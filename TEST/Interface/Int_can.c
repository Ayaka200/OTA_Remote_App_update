//
// Created by 23029 on 2026/9/9.
//

#include "Int_can.h"

/* ---------------- 软件接收环形队列：中断里写、主循环里读 ---------------- */
static CAN_Msg_t rx_queue[CAN_RX_QUEUE_LEN];
static volatile uint16_t rx_head = 0;   // 中断写入位置
static volatile uint16_t rx_tail = 0;   // 主循环读取位置

void Int_CAN_Init(void) {
    /*1. 过滤器配置*/
    CAN_FilterTypeDef FilterConfig;
    // 1.选择过滤器0号(0~13)
    FilterConfig.FilterBank = 0;
    // 2.配置为32位掩码模式
    FilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    FilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    // 3.配置32位屏蔽段和掩码段（全0 = 不过滤，接收所有ID）
    //只要上位机ID为1的程序
    FilterConfig.FilterIdHigh = 0x0020;
    FilterConfig.FilterIdLow = 0x0000;
    FilterConfig.FilterMaskIdHigh = 0xFFE0 ;
    FilterConfig.FilterMaskIdLow = 0x0000;

    FilterConfig.FilterActivation = ENABLE;
    FilterConfig.FilterFIFOAssignment = CAN_FILTER_FIFO1;   // 与已使能的 CAN1_RX1 中断对应

    HAL_CAN_ConfigFilter(&hcan, &FilterConfig);
    /*2. 手动开启CAN*/
    HAL_CAN_Start(&hcan);
    /*3. 使能 FIFO1 接收中断：有帧挂起时进回调 */
    HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO1_MSG_PENDING);
}

/**
 * @brief CAN设备向总线发送消息
 * @param TxHeader  CAN发送消息结构体
 * @param data 发送数据
 * @return 发送结果
 */
HAL_StatusTypeDef Int_CAN_Send(CAN_TxHeaderTypeDef *TxHeader, uint8_t *data) {

    uint32_t TxMailbox;
    // 1. 将要发送的信息添加到邮箱
    if (HAL_CAN_AddTxMessage(&hcan, TxHeader, data, &TxMailbox) != HAL_OK) {
        return HAL_ERROR;
    }

    uint32_t tick = HAL_GetTick();                       // 进循环前记下起始时刻
    while (HAL_CAN_IsTxMessagePending(&hcan, TxMailbox) != 0) {   // 还在忙
        if (HAL_GetTick() - tick > 500) return HAL_TIMEOUT;      // 超过 500ms 强制退出，不死等
    }
    return HAL_OK;
}



/**
 * @brief 主循环从软件队列取出一条报文
 * @param out 输出：读到的完整报文
 * @return 1=取到一条；0=队列为空
 */
uint8_t Int_CAN_Pop(CAN_Msg_t *out) {
    if (rx_head == rx_tail) return 0;               // 空
    *out = rx_queue[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1U) % CAN_RX_QUEUE_LEN);
    return 1;
}

/**
 * @brief FIFO1 接收中断回调：只负责把帧从硬件FIFO搬进软件队列，不做耗时处理
 */
void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef *hcanx) {
    uint16_t next = (uint16_t)((rx_head + 1U) % CAN_RX_QUEUE_LEN);
    if (next != rx_tail) {                          // 队列没满才存；满了直接丢最新帧
        CAN_Msg_t *slot = &rx_queue[rx_head];
        if (HAL_CAN_GetRxMessage(hcanx, CAN_RX_FIFO1,
                                 &slot->header, slot->data) == HAL_OK) {
            rx_head = next;
                                 }
    }
}
