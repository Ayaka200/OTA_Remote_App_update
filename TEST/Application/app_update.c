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
 *   0x103 -> 校验扇区 CRC，通过后擦除并写入 W25Q64，然后发 ACK
 *   0x105/0x106 -> 校验全镜像 CRC，成功才算升级完成
 *
 * 关键点：CRC 是"边收边算"的，不会额外阻塞接收；
 *         真正的耗时操作是 W25Q64 擦写，所以用 Int_can 的 256 帧队列吸收。
 */
uint8_t uart_cmd_buff[UART_CMD_LEN] = {0};

/* 这个变量会在串口中断里被改，在主循环里被读，所以必须加 volatile */
volatile UPDATE_STATUS update_status = UPDATE_IDLE;

uint8_t EXPECTED[4] = {0xAA, 0xBB, 0xAA, 0xBB};

/* 仅用于调试观测 */
uint32_t can_recv_msg_len   = 0U;
uint32_t can_recv_full_crc  = 0U;

CAN_TxHeaderTypeDef txHeader;

/* ---------------- 接收状态：必须用 static/global，不能放局部变量 ---------------- */
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
static uint32_t s_flash_addr = NEW_IMAGE_DATA_ADDR; /* W25Q64中新固件数据的起始地址 */
static New_Image_Header new_image_header;

static uint8_t App_Verify_W25Q64_Image(void);   /* 回读校验，定义在后面 */

/**
 * @brief 与 Sendsoft 完全一致的 CRC32 逐字节更新
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

    txHeader.StdId = UPDATE_CMD_CAN_ID;
    txHeader.IDE = CAN_ID_STD;
    txHeader.RTR = CAN_RTR_DATA;
    txHeader.TransmitGlobalTime = DISABLE;

    new_image_header.magic = 0x55AA55AA;
    new_image_header.reserved = NEW_IMAGE_LAYOUT_TAG;   /* Bootloader 靠它判断布局是否匹配 */

}

/**
 * @brief 向 Sendsoft 发送请求更新指令
 */
void App_Update_SendCmd(void)
{
    CAN_Msg_t stale_msg;

    /*
     * 开始新一轮之前，先把队列里上一轮残留的帧丢掉。
     * 否则旧的数据帧/结束帧会被当成新一包的数据解析，导致本次升级直接失败。
     */
    while (Int_CAN_Pop(&stale_msg) != 0U) {
    }

    txHeader.ExtId = 0x000;
    txHeader.DLC = 4;
    if (Int_CAN_Send(&txHeader, EXPECTED) == HAL_OK) {
        update_status = UPDATE_RECV;
        printf("Send CAN ID succeed\r\n");
    }
}

/**
 * @brief 收到0x100起始帧：复位一次接收状态
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
    s_flash_addr = NEW_IMAGE_DATA_ADDR;
    can_recv_msg_len = 0U;
    can_recv_full_crc = 0U;

    /*
     * 先把上一包固件的头部擦掉：
     * 这样即使本次传输中途断掉，Bootloader 也读不到"旧头部 + 新数据"的组合。
     */
    Int_W25Q64_EraseSector(NEW_IMAGE_ADDR);

    printf("Recv Start\r\n");
}

/**
 * @brief 收到0x101扇区序号：开始接收一个新扇区
 */
static void App_Recv_SectorNum(uint8_t sector_num)
{
    if (s_recv_started == 0U) {
        return;
    }

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
 * @brief 接收方每写完一次扇区向发送方发送应答
 */
static uint8_t App_FlashWriteSector_ACK(void) {
    txHeader.StdId = 0x001;
    txHeader.DLC = 1;
    uint8_t ack = 0x66;
    if (Int_CAN_Send(&txHeader, &ack) == HAL_OK) {
        return 1;
    }
    return 0;
}

static uint8_t App_FlashWriteHeader(void) {
    new_image_header.crc32 = s_expected_image_crc;
    new_image_header.length = s_total_len;
    // printf("write header: magic=%08X len=%d\r\n",
    //        new_image_header.magic, new_image_header.length);

    /*
     * 头部独占 0x200000 那个 4KB 扇区，数据区从 0x201000 开始，
     * 所以数据的擦除不会再顺手擦掉头部区，这里必须自己先擦干净再写。
     */
    Int_W25Q64_EraseSector(NEW_IMAGE_ADDR);
    Int_W25Q64_PageProgram(NEW_IMAGE_ADDR,(uint8_t *)&new_image_header,sizeof(new_image_header));

    // uint32_t readback_magic = 0;
    // Int_W25Q64_ReadData(NEW_IMAGE_ADDR, (uint8_t *)&readback_magic, 4);
    // printf("readback magic=%08X\r\n", readback_magic);
    return 1;
}

/**
 * @brief 把一个4KB扇区写入W25Q64
 */
static void App_FlashWriteSector(void)
{
    uint32_t addr;
    uint16_t offset = 0U;

    if (s_sector_len == 0U) {
        return;
    }

    /* 数据区按 4KB 对齐：第 seq 个分块正好占满第 seq 个 4KB 扇区，不会再跨扇区 */
    addr = s_flash_addr + ((uint32_t)s_sector_seq * APP_RX_BUF_SIZE);

    /* W25Q64 一个扇区 4KB，写前必须擦除 */
    //printf("sr before erase=%02X\r\n", Int_W25Q64_ReadStatus());
    Int_W25Q64_EraseSector(addr);
    //printf("sr after erase=%02X\r\n", Int_W25Q64_ReadStatus());


    /* 按 256B 页写，最后一页可以不足 256B */
    while (offset < s_sector_len) {
        uint16_t chunk = (uint16_t)(s_sector_len - offset);
        if (chunk > W25Q_PAGE_SIZE) {
            chunk = W25Q_PAGE_SIZE;
        }

        Int_W25Q64_PageProgram(addr + offset, &s_sector_buf[offset], chunk);
        offset = (uint16_t)(offset + chunk);
    }

    /* 写完发一次ACK，不重试 */
    printf("write finish %d\r\n",s_sector_seq);
    // if (s_sector_seq == 0) {
    //     uint8_t verify[8];
    //     Int_W25Q64_ReadData(0x200100, verify, 8);
    //     printf("fw verify: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
    //            verify[0], verify[1], verify[2], verify[3],
    //            verify[4], verify[5], verify[6], verify[7]);
    // }
    App_FlashWriteSector_ACK();
}

/**
 * @brief 收到0x103扇区CRC：校验通过才写W25Q64
 */
static void App_Recv_SectorCrc(uint32_t crc_from_sender)
{
    if (s_recv_started == 0U || s_update_error != 0U) {
        return;
    }

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
 */
static void App_Recv_EndFrame(void)
{
    printf("recv_len=%lu total_len=%lu img_crc=%08lX expect=%08lX drop=%lu\r\n",
           (unsigned long)s_recv_len, (unsigned long)s_total_len,
           (unsigned long)(~s_image_crc), (unsigned long)s_expected_image_crc,
           (unsigned long)Int_CAN_GetDropCount());
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

    if (s_update_error == 0U) {
        /* 1. 写头部 */
        App_FlashWriteHeader();

        /* 2. 回读 W25Q64 再算一遍 CRC：存储这一步坏了就别通知 Bootloader */
        if (App_Verify_W25Q64_Image() == 0U) {
            printf("W25Q64 verify FAIL, abort update\r\n");
            s_update_error = 1U;
        }
    }

    update_status = (s_update_error == 0U) ? UPDATE_END : UPDATE_ERROR;
    if (update_status == UPDATE_END) {
        printf("Download End\r\n");
    }


}

/**
 * @brief 回读 W25Q64 里的新固件，重新算一遍 CRC32
 *
 * 这一步是"升级是否真的落地"的最后一道闸：
 * 只有 W25Q64 里存的东西和 Sendsoft 发来的完全一致，才允许写升级标志。
 * 存储坏了就在这里失败，而不是复位后让 Bootloader 把坏镜像烧进 A 区。
 *
 * @return 1=校验通过  0=校验失败
 */
static uint8_t App_Verify_W25Q64_Image(void)
{
    New_Image_Header hdr;
    static uint8_t read_buf[W25Q_PAGE_SIZE];   /* 放静态区，别占栈 */
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t offset;

    /* 1. 头部回读 */
    Int_W25Q64_ReadData(NEW_IMAGE_ADDR, (uint8_t *)&hdr, sizeof(hdr));
    if (hdr.magic != 0x55AA55AAUL || hdr.reserved != NEW_IMAGE_LAYOUT_TAG) {
        printf("verify: bad header magic=%08lX tag=%08lX\r\n",
               (unsigned long)hdr.magic, (unsigned long)hdr.reserved);
        return 0U;
    }
    if (hdr.length != s_total_len || hdr.crc32 != s_expected_image_crc) {
        printf("verify: header len=%lu crc=%08lX\r\n",
               (unsigned long)hdr.length, (unsigned long)hdr.crc32);
        return 0U;
    }

    /* 2. 数据回读 + 重算整包 CRC */
    for (offset = 0U; offset < hdr.length; offset += sizeof(read_buf)) {
        uint32_t chunk = hdr.length - offset;
        if (chunk > sizeof(read_buf)) {
            chunk = sizeof(read_buf);
        }
        Int_W25Q64_ReadData(NEW_IMAGE_DATA_ADDR + offset, read_buf, chunk);
        for (uint32_t i = 0U; i < chunk; i++) {
            crc = App_CRC_Calculate(read_buf[i], crc);
        }
    }

    printf("verify: w25q crc=%08lX expect=%08lX\r\n",
           (unsigned long)(~crc), (unsigned long)hdr.crc32);
    return ((~crc) == hdr.crc32) ? 1U : 0U;
}

static void App_Update_Ready(void) {
    /* 程序确认无误后更新标志位，准备更新 */
    if (Int_AT24C02_Read_Byte(BOOT_CHECK_KEY_ADDR) != BOOT_CHECK_KEY) {
        /* 校验位丢了就自己补上，别让这次升级因为一个字节白跑 */
        Int_AT24C02_Write_Byte(BOOT_CHECK_KEY_ADDR, BOOT_CHECK_KEY);
    }
    Int_AT24C02_Write_Byte(BOOT_FLAG_CHECK_ADDR, BOOT_FLAG_REQ_UPDATE);

    /* 回读确认真的写进 EEPROM 了 */
    if (Int_AT24C02_Read_Byte(BOOT_FLAG_CHECK_ADDR) == BOOT_FLAG_REQ_UPDATE) {
        printf("Change Status Success\r\n");
    } else {
        printf("EEPROM flag write FAIL\r\n");
        s_update_error = 1U;
    }

    update_status = (s_update_error == 0U) ? UPDATE_IDLE : UPDATE_ERROR;
}

/**
 * @brief 解析Int_can软件队列里的所有CAN帧
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
