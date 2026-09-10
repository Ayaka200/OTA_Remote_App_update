//
// Created by 23029 on 2026/9/9.
//

#ifndef TEST_INT_CAN_H
#define TEST_INT_CAN_H

#include "main.h"
#include "can.h"

#define CAN_RX_QUEUE_LEN  32U     /* 软件接收队列深度（环形留一空槽，实际存 LEN-1 条） */

/* 一条完整 CAN 报文：帧头 + 最多 8 字节数据 */
typedef struct {
    CAN_RxHeaderTypeDef header;
    uint8_t data[8];
} CAN_Msg_t;

void Int_CAN_Init(void);

HAL_StatusTypeDef Int_CAN_Send(CAN_TxHeaderTypeDef *TxHeader, uint8_t *data);

uint8_t Int_CAN_Pop(CAN_Msg_t *out);   /* 主循环取一条：1=取到，0=队列空 */

#endif //TEST_INT_CAN_H
