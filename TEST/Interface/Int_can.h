//
// Created by 23029 on 2026/9/9.
//

#ifndef TEST_INT_CAN_H
#define TEST_INT_CAN_H

#include "main.h"
#include "can.h"

/*
 * ================= 软件接收队列深度 =================
 *
 * 一条 CAN_Msg_t 约 36 字节，256 帧约 9.2KB。
 *
 * 为什么需要这么大：
 *   接收端解析到 0x103 扇区CRC后，要擦除并写入 W25Q64。
 *   这期间主循环会阻塞，不能及时从队列取帧。
 *   发送端 Sendsoft 还在继续发，所以必须先用这个软件队列把CAN帧接住。
 *
 * 队列在中断里写、主循环里读，实际大小由 CAN_RX_QUEUE_LEN 决定。
 */
#define CAN_RX_QUEUE_LEN  256U

/*
 * 一条完整CAN报文：
 *   header：STM32 HAL 解析出的标准ID、DLC等信息
 *   data  ：最多8字节数据
 *
 * 注意：这里存的是“整个CAN帧”，不是裸字节。
 *       这样做的好处是主循环可以直接根据 header.StdId 判断协议类型，
 *       不需要自己从字节流里找帧边界。
 */
typedef struct {
    CAN_RxHeaderTypeDef header;
    uint8_t data[8];
} CAN_Msg_t;

void Int_CAN_Init(void);

HAL_StatusTypeDef Int_CAN_Send(CAN_TxHeaderTypeDef *TxHeader, uint8_t *data);

/*
 * 主循环取一帧：
 *   返回1：out里是取到的完整CAN帧
 *   返回0：软件队列为空
 */
uint8_t Int_CAN_Pop(CAN_Msg_t *out);

/*
 * 软件队列满导致的新帧丢弃计数：
 *   正常升级结束时应该为0；
 *   如果大于0，说明主循环处理不过来，本次升级数据可能不完整。
 */
uint32_t Int_CAN_GetDropCount(void);

#endif //TEST_INT_CAN_H
