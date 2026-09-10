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

/**
 * @brief 对上位机的程序进行初始化
 */
void App_Update_Init(void) {
    printf("app_update_init\r\n");
    Int_CAN_Init();
    app_update_status = APP_UPDATE_WAIT;
    /* 初始化CAN发送帧头固定字段 */
    TxHeader.StdId = 0x001;
    TxHeader.ExtId = 0x000;
    TxHeader.IDE = CAN_ID_STD;
    TxHeader.RTR = CAN_RTR_DATA;
    TxHeader.TransmitGlobalTime = DISABLE;
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
                    app_update_status = APP_UPDATE_SEND_APP;
                }

            }
        }
        cnt++;
    }
}
/**
 * @brief 发送程序
 */
void App_Update_SendApp(void) {

    if (update_data_len < uart_recv_full_len) {
        // 读取待发送程序的8字节
        uint8_t chunk = (uart_recv_full_len - update_data_len)>8 ? 8 : uart_recv_full_len-update_data_len;
        for (uint8_t i = 0; i < chunk; i++) {
            can_buff[i] = *(volatile uint8_t *)(APP_START_ADDR+update_data_len+i);
        }
        update_data_len += chunk;

        // 动态设置本次发送的字节数（最后一包可能不足8字节）
        TxHeader.DLC = chunk;
        // 发送程序数据
        Int_CAN_Send(&TxHeader, can_buff);
        if (update_data_len % 256 == 0) {

            HAL_Delay(100);

        }
    }
    else {
        app_update_status = APP_UPDATE_WAIT;
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
            printf("App_Update_SendApp\r\n");
            HAL_Delay(1000);
            App_Update_SendApp();
            break;

        default:
            break;
    }
}
