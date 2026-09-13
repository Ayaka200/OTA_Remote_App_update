//
// Created by 23029 on 2026/9/12.
//

#include "app_update.h"

#include <string.h>

#include "int_at24c02.h"
#include "int_w25q64.h"

/*
 * ================= TEST 端接收流程（和 Sendsoft 对应） =================
 *
 * Sendsoft 发送顺序：
 *   0x100 起始帧
 *   0x101 扇区序号
 *   0x102 程序数据（每帧最多 8 字节）
 *   0x103 当前 4KB 扇区的 CRC32
 *   0x104 总长度
 *   0x105 整包镜像 CRC32
 *   0x106 结束帧
 *
 * TEST 接收流程：
 *   CAN 中断 -> Int_can 软件帧队列 -> App_Update_Recv() 解析
 *   数据帧 -> 写入 s_sector_buf[4096]，同时更新扇区 CRC 和全镜像 CRC
 *   0x103 -> 校验扇区 CRC，通过后擦除并写入 W25Q64
 *   0x105/0x106 -> 校验全镜像 CRC，成功才算升级完成
 *
 * 关键点：CRC 是“边收边算”的，不会额外阻塞接收；
 *         真正的耗时操作是 W25Q64 擦写，所以用 Int_can 的 256 帧队列吸收。
 */
uint8_t uart_cmd_buff[UART_CMD_LEN] = {0};

/* 这个变量会在串口中断里被改，在主循环里被读，所以必须加 volatile */
volatile UPDATE_STATUS update_status = UPDATE_IDLE;

uint8_t EXPECTED[4] = {0xAA, 0xBB, 0xAA, 0xBB};

/* 仅用于调试观测 */
uint32_t can_recv_msg_len   = 0U;
uint32_t can_recv_full_crc  = 0U;

/* ---------------- 接收状态：必须用 static/global，不能放局部变量 ---------------- */
/*
 * 原来的代码把这些变量写在 App_Update_Recv() 函数里面。
 * 但 App_Update_Recv() 会被主循环反复调用，函数一返回局部变量就没了，
 * 所以 CRC 根本无法跨越多条 0x102 数据帧累积，状态也会每次都清零。
 * 因此这些变量一定要放在函数外面。
 */
static uint8_t  s_sector_buf[APP_RX_BUF_SIZE];   /* 暂存一个4KB扇区，等CRC通过后写Flash */
static uint16_t s_sector_len = 0U;               /* 当前扇区已经收到多少字节 */
static uint8_t  s_sector_seq = 0U;               /* 当前扇区序号，来自0x101帧 */
static uint8_t  s_expected_sector_seq = 0U;      /* 期望的下一个扇区序号，用来检测丢扇区 */
static uint32_t s_sector_crc = 0xFFFFFFFFUL;     /* 当前扇区CRC，每收1字节更新一次 */
static uint32_t s_image_crc = 0xFFFFFFFFUL;      /* 整包镜像CRC，跨扇区连续更新 */
static uint32_t s_expected_image_crc = 0U;       /* Sendsoft发来的整包CRC，最后用来比较 */
static uint32_t s_recv_len = 0U;                 /* TEST实际收到的数据总字节数 */
static uint32_t s_total_len = 0U;                /* Sendsoft 0x104帧发来的总长度 */
static uint8_t  s_recv_started = 0U;             /* 是否已经收到0x100起始帧 */
static uint8_t  s_update_error = 0U;             /* 1=本次升级出现过CRC/长度/丢帧错误 */
static uint32_t s_flash_addr = NEW_IMAGE_ADDR;   /* W25Q64中新固件的起始地址 */

/**
 * @brief 与 Sendsoft 完全一致的 CRC32 逐字节更新
 * @param byte 本次要加入计算的数据字节
 * @param crc  上一轮的CRC结果
 * @return     加入byte后的CRC结果
 *
 * 算法参数：
 *   初值 init = 0xFFFFFFFF
 *   多项式 poly = 0x04C11DB7
 *   每个字节先异或到最高8位，然后循环移位8次。
 *
 * 注意：Sendsoft在发送0x103/0x105之前会做一次按位取反，
 *       所以TEST端比较时要用 (~本地CRC) 和收到的CRC比较。
 */
static uint32_t App_CRC_Calculate(uint8_t byte, uint32_t crc)
{
    crc ^= (uint32_t)byte << 24;
    for (uint8_t i = 0U; i < 8U; i++) {
        crc = (crc & 0x80000000UL) ? (crc << 1) ^ 0x04C11DB7UL : (crc << 1);
    }
    return crc;
}

/**
 * @brief 读取 Sendsoft 发来的4字节小端数
 * @param p 指向CAN数据区的指针
 * @return 组合后的uint32_t
 *
 * Sendsoft的App_Send_CRC()是按低字节在前发送的，
 * 所以这里必须用 little-endian 方式拼接。
 */
static uint32_t App_ReadU32LE(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}


/**
 * @brief 更新 UART 事件回调：判断 "new version"，并保证字符串有 '\0'
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance != USART1) {
        return;
    }

    if (Size >= UART_CMD_LEN) {
        Size = UART_CMD_LEN - 1U;
    }
    uart_cmd_buff[Size] = '\0';

    if (update_status == UPDATE_IDLE && strstr((char *)uart_cmd_buff, "new version") != NULL) {
        update_status = UPDATE_CMD_SR;
        return;
    }

    /* 没匹配到命令，继续等待下一次串口命令 */
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    __HAL_UART_CLEAR_IDLEFLAG(&huart1);
    HAL_UARTEx_ReceiveToIdle_IT(&huart1, uart_cmd_buff, UART_CMD_LEN);
}

/**
 * @brief 正常 App 功能
 */
static void App_Run(void)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
    HAL_Delay(500);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    HAL_Delay(500);
}

/**
 * @brief 更新初始化：上电只调用一次
 */
void App_Update_Init(void)
{
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    __HAL_UART_CLEAR_IDLEFLAG(&huart1);
    HAL_UARTEx_ReceiveToIdle_IT(&huart1, uart_cmd_buff, UART_CMD_LEN);

    Int_CAN_Init();
}

/**
 * @brief 向 Sendsoft 发送请求更新指令
 */
void App_Update_SendCmd(void)
{
    CAN_TxHeaderTypeDef txHeader = {
        .StdId = UPDATE_CMD_CAN_ID,
        .ExtId = 0x000,
        .IDE = CAN_ID_STD,
        .RTR = CAN_RTR_DATA,
        .DLC = 4,
        .TransmitGlobalTime = DISABLE,
    };

    if (Int_CAN_Send(&txHeader, EXPECTED) == HAL_OK) {
        update_status = UPDATE_RECV;
        printf("Send CAN ID succeed\r\n");
    }
}

/**
 * @brief 收到0x100起始帧：复位一次接收状态
 *
 * 每次升级开始时，Sendsoft会先发0x100。
 * 这里把上一包可能遗留的长度、CRC、错误标志全部清零。
 */
static void App_Recv_StartFrame(void)
{
    s_recv_started = 1U;
    s_sector_len = 0U;
    s_sector_seq = 0U;
    s_expected_sector_seq = 0U;
    s_sector_crc = 0xFFFFFFFFUL;
    s_image_crc  = 0xFFFFFFFFUL;
    s_expected_image_crc = 0U;
    s_recv_len = 0U;
    s_total_len = 0U;
    s_update_error = 0U;
    s_flash_addr = NEW_IMAGE_ADDR;
    can_recv_msg_len = 0U;
    can_recv_full_crc = 0U;
}

/**
 * @brief 收到0x101扇区序号：开始接收一个新扇区
 * @param sector_num Sendsoft发来的扇区号
 *
 * 正常顺序：
 *   0x101 -> 很多0x102 -> 0x103 -> 下一个0x101 ...
 *
 * 如果收到0x101时 s_sector_len 还不为0，
 * 说明上一个扇区没有收到0x103，数据不完整，标记错误。
 */
static void App_Recv_SectorNum(uint8_t sector_num)
{
    if (s_recv_started == 0U) {
        return;
    }

    /*
     * 正常情况下，上一个扇区应在 0x103 帧处已经 CRC 校验并写入。
     * 如果这里 s_sector_len 还不为 0，说明上一个扇区没有收到 0x103。
     */
    if (s_sector_len != 0U) {
        s_update_error = 1U;
    }

    if (sector_num != s_expected_sector_seq) {
        s_update_error = 1U;
    }
    s_expected_sector_seq = (uint8_t)(sector_num + 1U);

    s_sector_seq = sector_num;
    s_sector_len = 0U;
    s_sector_crc = 0xFFFFFFFFUL;
}

/**
 * @brief 收到0x102数据帧：写入扇区缓存并同时更新扇区CRC和整镜像CRC
 * @param data CAN帧里的数据，最多8字节
 * @param len  DLC，通常为8，最后一帧可能小于8
 *
 * 这个函数一次循环同时完成三件事：
 *   1. 把字节放进4KB扇区缓存；
 *   2. 更新当前扇区的CRC；
 *   3. 更新整包镜像CRC。
 *
 * 因为CRC是边收边算的，不需要等数据全收完再回头读一遍，
 * 所以这个函数执行时间很短，不会成为丢包瓶颈。
 * 真正耗时的操作是后面的W25Q64擦除和页写。
 */
static void App_Recv_Data(const uint8_t *data, uint8_t len)
{
    if (s_recv_started == 0U || s_update_error != 0U) {
        return;
    }

    if (len > 8U) {
        len = 8U;
    }

    if ((uint32_t)s_sector_len + len > APP_RX_BUF_SIZE) {
        s_update_error = 1U;
        return;
    }

    for (uint8_t i = 0U; i < len; i++) {
        uint8_t byte = data[i];

        s_sector_buf[s_sector_len] = byte;
        s_sector_len++;

        s_sector_crc = App_CRC_Calculate(byte, s_sector_crc);
        s_image_crc  = App_CRC_Calculate(byte, s_image_crc);
        s_recv_len++;
    }
}

/**
 * @brief 把一个4KB扇区写入W25Q64
 *
 * W25Q64是NOR Flash，有两个硬性限制：
 *   1. 写入前必须先擦除整个4KB扇区，不能直接覆盖写；
 *   2. 页写一次最多256字节，而且不能跨页。
 *
 * 所以这里先 EraseSector，再按256字节一页一页写。
 * 这个函数会阻塞主循环，阻塞期间到来的CAN帧
 * 由 Int_can.c 里的256帧软件队列先接住。
 */
static void App_FlashWriteSector(void)
{
    uint32_t addr;
    uint16_t offset = 0U;

    if (s_sector_len == 0U) {
        return;
    }

    addr = s_flash_addr + ((uint32_t)s_sector_seq * APP_RX_BUF_SIZE);

    /* W25Q64 一个扇区 4KB，写前必须擦除 */
    Int_W25Q64_EraseSector(addr);

    /* 按 256B 页写，最后一页可以不足 256B */
    while (offset < s_sector_len) {
        uint16_t chunk = (uint16_t)(s_sector_len - offset);
        if (chunk > W25Q_PAGE_SIZE) {
            chunk = W25Q_PAGE_SIZE;
        }

        Int_W25Q64_PageProgram(addr + offset, &s_sector_buf[offset], chunk);
        offset = (uint16_t)(offset + chunk);
    }
}

/**
 * @brief 收到0x103扇区CRC：校验通过才写W25Q64
 * @param crc_from_sender Sendsoft发来的扇区CRC
 *
 * Sendsoft发送前做了 crc = ~crc，
 * 所以TEST端要用 (~s_sector_crc) 和它比较。
 *
 * 注意：只有扇区CRC通过才执行Flash写入；
 *       如果CRC失败，只置错误标志，不写Flash，避免把坏数据写进去。
 */
static void App_Recv_SectorCrc(uint32_t crc_from_sender)
{
    if (s_recv_started == 0U || s_update_error != 0U) {
        return;
    }

    /* Sendsoft 发送前做了按位取反 */
    if ((~s_sector_crc) != crc_from_sender) {
        s_update_error = 1U;
        return;
    }

    App_FlashWriteSector();

    /* 当前扇区结束，准备下一扇区 */
    s_sector_len = 0U;
    s_sector_crc = 0xFFFFFFFFUL;
}

/**
 * @brief 收到0x104总长度
 * @param total_len Sendsoft发送的固件总字节数
 *
 * 这个帧是在所有数据发完后才到的，
 * 所以此时TEST端已经收到的 s_recv_len 应该等于 total_len。
 */
static void App_Recv_TotalLen(uint32_t total_len)
{
    s_total_len = total_len;
    can_recv_msg_len = total_len;

    if (s_recv_len != total_len) {
        s_update_error = 1U;
    }
}

/**
 * @brief 收到0x105整包镜像CRC
 * @param crc_from_sender Sendsoft发来的整包CRC（已按位取反）
 *
 * s_image_crc 从0x100之后开始，对所有0x102数据字节连续累计，
 * 不中途清零，所以它对应的是整包固件。
 */
static void App_Recv_ImageCrc(uint32_t crc_from_sender)
{
    s_expected_image_crc = crc_from_sender;
    can_recv_full_crc = crc_from_sender;

    if ((~s_image_crc) != crc_from_sender) {
        s_update_error = 1U;
    }
}

/**
 * @brief 收到0x106结束帧：做最终判断
 *
 * 只有下面三件事全部满足，才认为升级成功：
 *   1. TEST实际收到的长度 == Sendsoft发来的总长度；
 *   2. TEST算出的整包CRC == Sendsoft发来的整包CRC；
 *   3. Int_can没有因为软件队列满而丢过帧。
 * 否则进入 UPDATE_ERROR。
 */
static void App_Recv_EndFrame(void)
{
    if (s_recv_started == 0U) {
        s_update_error = 1U;
    }

    if (s_recv_len != s_total_len) {
        s_update_error = 1U;
    }

    if ((~s_image_crc) != s_expected_image_crc) {
        s_update_error = 1U;
    }

    if (Int_CAN_GetDropCount() != 0U) {
        s_update_error = 1U;
    }

    update_status = (s_update_error == 0U) ? UPDATE_END : UPDATE_ERROR;


}

static void App_Update_Ready(void) {
    // 程序确认无误后更新标志位，准备更新
    if (Int_AT24C02_Read_Byte(BOOT_CHECK_KEY_ADDR) == BOOT_CHECK_KEY) {
        Int_AT24C02_Write_Byte(BOOT_FLAG_CHECK_ADDR,BOOT_FLAG_REQ_UPDATE);
    }

}

/**
 * @brief 解析Int_can软件队列里的所有CAN帧
 *
 * 这里必须一直 Int_CAN_Pop()，直到队列真正为空。
 * 如果因为“App缓存可能满了”就提前停止Pop，
 * Int_can.c里的软件帧队列会很快被填满，
 * 软件队列满后，中断里只能丢弃新帧，就会丢包。
 *
 * switch中的每个case对应Sendsoft协议里的一个CAN ID。
 */
static void App_Update_Recv(void)
{
    CAN_Msg_t can_msg;

    while (Int_CAN_Pop(&can_msg) != 0U) {
        switch (can_msg.header.StdId) {
            case CAN_ID_START:          /* 0x100 起始帧 */
                App_Recv_StartFrame();
                break;

            case CAN_ID_SECTOR_NUM:     /* 0x101 扇区序号 */
                if (can_msg.header.DLC >= 1U) {
                    App_Recv_SectorNum(can_msg.data[0]);
                }
                break;

            case CAN_ID_DATA:           /* 0x102 程序数据 */
                App_Recv_Data(can_msg.data, (uint8_t)can_msg.header.DLC);
                break;

            case CAN_ID_SECTOR_CRC:     /* 0x103 扇区CRC，通过后写Flash */
                if (can_msg.header.DLC >= 4U) {
                    App_Recv_SectorCrc(App_ReadU32LE(can_msg.data));
                }
                break;

            case CAN_ID_TOTAL_LEN:      /* 0x104 总长度 */
                if (can_msg.header.DLC >= 4U) {
                    App_Recv_TotalLen(App_ReadU32LE(can_msg.data));
                }
                break;

            case CAN_ID_IMAGE_CRC:      /* 0x105 整包镜像CRC */
                if (can_msg.header.DLC >= 4U) {
                    App_Recv_ImageCrc(App_ReadU32LE(can_msg.data));
                }
                break;

            case CAN_ID_END:            /* 0x106 结束帧，最终校验 */
                App_Recv_EndFrame();
                break;

            default:
                /* 其他ID不处理，但仍然已经从队列里取出来了 */
                break;
        }
    }
}

/**
 * @brief 主循环中执行更新操作
 *
 * 状态机：
 *   UPDATE_IDLE    : 正常运行原App，等待UART "new version"
 *   UPDATE_CMD_SR  : 给Sendsoft发0x000更新请求
 *   UPDATE_RECV    : 解析Sendsoft发来的0x100~0x106
 *   UPDATE_END     : 全部CRC校验通过
 *   UPDATE_ERROR   : 过程中出错或发生过CAN丢帧
 */
void App_Update_Work(void)
{
    switch (update_status) {
        case UPDATE_IDLE:
            /* 还没进入升级流程，执行正常程序 */
            App_Run();
            break;

        case UPDATE_CMD_SR:
            /* 串口收到了 "new version"，向Sendsoft发更新请求 */
            App_Update_SendCmd();
            break;

        case UPDATE_RECV:
            /* 已经开始接收Sendsoft发来的固件数据 */
            App_Update_Recv();
            break;

        case UPDATE_END:
            /* 总长度、整包CRC、结束帧都校验通过 */
            App_Update_Ready();
            break;

        case UPDATE_ERROR:
            /* 本次升级失败，打印原因后回到空闲状态 */
            printf("Update error, can_drop=%lu\n",
                   (unsigned long)Int_CAN_GetDropCount());
            update_status = UPDATE_IDLE;
            break;

        default:
            break;
    }
}
