//
// Created by 23029 on 2026/9/10.
//

#include "App_update.h"
#include "app_bootloader.h"
#include <string.h>

#include "int_bootloader.h"

APP_UPDATE_STATUS app_update_status = APP_UPDATE_WAIT;
CAN_Msg_t rx_msg;
CAN_TxHeaderTypeDef TxHeader;   /* CAN发送帧头，定义在这里 */
//记录程序总长度
extern uint16_t uart_recv_full_len;
// 已经发送的长度
uint16_t update_data_len = 0;
// CAN发送缓存
uint8_t can_buff[8]={0};
// 扇区序列号(总扇数)
uint8_t Sector_Num = 0;
// CRC初始值
uint32_t crc32 = 0xFFFFFFFFUL;
// 当前扇区CRC
uint32_t sector_crc = 0xFFFFFFFFUL;
// 起始帧已发送标志
uint8_t tx_start_sent = 0;
// 收尾帧发送步骤
uint8_t tx_finish_step = 0;

uint8_t can_none = 0xFF;

static uint32_t App_CRC_Calculate(uint8_t Byte,uint32_t crc) {

    crc ^= (uint32_t)Byte << 24;
    for (int i = 0; i < 8; i++)
        crc = (crc & 0x80000000UL) ? (crc << 1) ^ 0x04C11DB7UL : (crc << 1);
    return crc;
}

void App_Send_Start(void) {

    TxHeader.StdId = 0x100;
    TxHeader.DLC = 1;
    Int_CAN_Send(&TxHeader, &can_none);
}

static void App_Send_Sector_Num(void) {

    TxHeader.StdId = 0x101;
    TxHeader.DLC = 1;
    Int_CAN_Send(&TxHeader, &Sector_Num);
    Sector_Num++;
}

static void App_Send_Data(uint8_t * Data,uint8_t len) {
    TxHeader.StdId = 0x102;
    TxHeader.DLC = len;
    Int_CAN_Send(&TxHeader, Data);
}

static uint8_t App_Recv_ACK(void) {
    CAN_Msg_t ack_msg;
    if (Int_CAN_Pop(&ack_msg) == 0) return 0;   /* 队列为空 */
    if (ack_msg.header.StdId == 0x001 && ack_msg.header.DLC == 1) {
        if (ack_msg.data[0] == 0x66) {
            return 1;
        }
    }
    return 0;   /* 不是ACK */
}

void App_Send_CRC(uint32_t CRC32,uint32_t id) {
    TxHeader.StdId = id;
    TxHeader.DLC = 4;
    uint8_t crc_buf[4];
    for (int i = 0; i < 4; i++) {
        crc_buf[i] = (CRC32 >> (i*8)) & 0xFF;
    }
    Int_CAN_Send(&TxHeader, crc_buf);
}

void App_Send_End(void) {
    TxHeader.StdId = 0x106;
    TxHeader.DLC = 1;
    Int_CAN_Send(&TxHeader, &can_none);
}
/**
 * @brief 对上位机的程序进行初始化
 */
void App_Update_Init(void) {
    printf("app_update_init\r\n");
    Int_CAN_Init();
    app_update_status = APP_UPDATE_WAIT;
    /* 初始化CAN发送帧头固定字段 */
    TxHeader.ExtId = 0x000;
    TxHeader.IDE = CAN_ID_STD;
    TxHeader.RTR = CAN_RTR_DATA;
    TxHeader.TransmitGlobalTime = DISABLE;

    /* 重置所有发送状态 */
    Sector_Num = 0;
    crc32 = 0xFFFFFFFFUL;
    sector_crc = 0xFFFFFFFFUL;
    update_data_len = 0;
    tx_start_sent = 0;
    tx_finish_step = 0;
}

/**
 * @brief 等待发送指令
 */
void App_Update_WaitCmd(void) {

    uint8_t cnt = 0;
    const uint8_t EXPECTED[4] = {0xAA,0xBB,0xAA,0xBB};      //期望发送指令
    const uint8_t MAX_PROCESS = 8;       // 每次最多处理8条，避免主循环卡住
    while (cnt < MAX_PROCESS && Int_CAN_Pop(&rx_msg)) {
        // 标准ID为0x000的视为接收方的指令
        if (rx_msg.header.StdId == 0x000) {
            //DLC为4，且内容与发送指令匹配
            if (rx_msg.header.DLC == 4) {
                if (memcmp(rx_msg.data,EXPECTED,4) == 0) {
                    printf("CMD correct\r\n");
                    printf("fw_len=%d\r\n", uart_recv_full_len);
                    printf("sent=%d\r\n", (int)update_data_len);
                    printf("start_sent=%d\r\n", (int)tx_start_sent);
                    app_update_status = APP_UPDATE_SEND_APP;

                }

            }
        }
        cnt++;
    }
}
/**
 * @brief 发送程序
 * CANID : 0x100 : 起始帧 ,0x101 :扇区序列号 ,0x102 : 程序数据 , 0x103 :扇区CRC32, 0x104 :总长度
 *         0x105 :全镜像CRC32, 0x106 : 结束帧
 */
void App_Update_SendApp(void) {

    // printf("SendApp: start_sent=%d, sent=%d, total=%d, finish_step=%d\r\n",
    //        tx_start_sent, update_data_len, uart_recv_full_len, tx_finish_step);
    /* ---------- 第一步：发起始帧（只发一次） ---------- */
    if (!tx_start_sent) {
        App_Send_Start();
        tx_start_sent = 1;
        return;
    }

    /* ---------- 第二步：循环发扇区 ---------- */
    if (update_data_len < uart_recv_full_len) {

        /* 扇区起始：发扇区序号 + 重置扇区CRC */
        if (update_data_len % 4096 == 0) {
            App_Send_Sector_Num();
            sector_crc = 0xFFFFFFFFUL;
        }

        /* 发一帧数据（8字节） */
        uint8_t chunk = uart_recv_full_len-update_data_len > 8 ? 8 : uart_recv_full_len-update_data_len;
        for (uint8_t i = 0; i < chunk; i++) {
            /* 注意：这里必须是 = 赋值。原来写成 == 只是比较，不会把Flash数据放进can_buff */
            can_buff[i] = *(volatile uint8_t *)(APP_START_ADDR+update_data_len+i);
            /* 流式计算：扇区CRC + 全镜像CRC 一起算 */
            sector_crc = App_CRC_Calculate(can_buff[i],sector_crc);
            crc32      = App_CRC_Calculate(can_buff[i], crc32);
        }
        App_Send_Data(can_buff,chunk);
        update_data_len += chunk;

        /* 扇区结束：发扇区CRC */
        if ( update_data_len % 4096 == 0 || update_data_len >= uart_recv_full_len) {
            sector_crc = ~sector_crc;          /* 标准CRC32最后取反 */
            App_Send_CRC(sector_crc, 0x103);   /* 0x103 = 扇区CRC */
            /* 等待TEST应答，超时2秒 */
            uint32_t ack_tick = HAL_GetTick();
            while (!App_Recv_ACK()) {
                if (HAL_GetTick() - ack_tick > 20000) {
                    printf("ACK timeout, sector=%d\r\n", Sector_Num - 1);
                    app_update_status = APP_UPDATE_WAIT;
                    return;
                }
            }
        }

    }
    /* ---------- 第三步：全部发完，发收尾帧 ---------- */
    else {
        switch (tx_finish_step) {
            case 0: {
                /* 发总长度（0x104） */
                TxHeader.StdId = 0x104;
                TxHeader.DLC = 4;
                uint8_t len_buf[4] = {
                    uart_recv_full_len & 0xFF,
                    (uart_recv_full_len >> 8)  & 0xFF,
                    (uart_recv_full_len >> 16) & 0xFF,
                    (uart_recv_full_len >> 24) & 0xFF
                };
                Int_CAN_Send(&TxHeader, len_buf);
                tx_finish_step = 1;
                break;
            }
            case 1:
                /* 发全镜像CRC（0x105） */
                crc32 = ~crc32;                 /* 最后取反 */
                App_Send_CRC(crc32, 0x105);
                tx_finish_step = 2;
                break;

            case 2:
                /* 发结束帧（0x106） */
                App_Send_End();
                tx_finish_step = 3;
                break;

            case 3:
                /* 重置所有状态，回到等待 */
                App_Update_Init();
                break;
        }
    }
}
/**
 * @brief 上位机程序上传APP执行
 */
void App_Update_work(void) {
    switch (app_update_status) {
        case APP_UPDATE_WAIT:
            printf("App_Update_WaitCmd\r\n");
            HAL_Delay(1000);
            App_Update_WaitCmd();
            break;

        case APP_UPDATE_SEND_APP:
            // printf("App_Update_SendApp\r\n");
            // HAL_Delay(1000);
            App_Update_SendApp();
            break;

        default:
            break;
    }
}
