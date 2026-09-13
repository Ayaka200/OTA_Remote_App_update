//
// Created by 23029 on 2026/9/9.
//

#include "Int_can.h"

/*
 * ================= CAN 接收队列的工作原理 =================
 *
 * CAN 中断是“生产者”，主循环 App_Update_Recv() 是“消费者”。
 * 两者之间用一个环形队列 rx_queue 解耦：
 *
 *   生产者：HAL_CAN_RxFifo1MsgPendingCallback()
 *           CAN 硬件 FIFO 收到帧 -> 中断 -> 写入 rx_queue
 *
 *   消费者：App_Update_Recv()
 *           Int_CAN_Pop() 从 rx_queue 取帧 -> 解析/计算CRC/写Flash
 *
 * 两个下标：
 *   rx_head：中断写入位置，只由CAN中断修改；
 *   rx_tail：主循环读取位置，只由Int_CAN_Pop()修改。
 *
 * 判断空/满：
 *   rx_head == rx_tail                 -> 队列空
 *   (rx_head+1)&mask == rx_tail        -> 队列满
 *
 * 为什么加 volatile：
 *   rx_head 会被CAN中断修改，主循环要能看到最新值；
 *   rx_tail 会被主循环修改，中断判断“队列满”时也要能看到最新值。
 *   不加 volatile，编译器可能把它们缓存进寄存器，导致两边看到的不是同一份数据。
 */
static CAN_Msg_t rx_queue[CAN_RX_QUEUE_LEN];  /* 软件接收环形队列，每项是一条完整CAN帧 */
static volatile uint16_t rx_head = 0U;        /* 中断写入位置 */
static volatile uint16_t rx_tail = 0U;        /* 主循环读取位置 */
static volatile uint32_t rx_drop_count = 0U;  /* 软件队列满时丢弃的新帧数量 */

/*
 * 一条 CAN_Msg_t 约36字节，256帧约9.2KB。
 * 主循环执行 W25Q64 扇区擦除/页写时会阻塞，
 * 这段时间收到的CAN帧就先放在这里，防止一帧都存不下。
 */

void Int_CAN_Init(void) {
    /*1. 过滤器配置*/
    CAN_FilterTypeDef FilterConfig;
    // 1.选择过滤器0号(0~13)
    FilterConfig.FilterBank = 0;
    // 2.配置为32位掩码模式
    FilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    FilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;

    /*
     * 接收 Sendsoft 的 0x100 ~ 0x107。
     * STID[10:3] 必须等于 0x100>>3 = 0x20，低 3 位忽略。
     * 若只调试，也可以把 Id 和 Mask 全置 0，接收所有标准帧。
     */
    FilterConfig.FilterIdHigh     = (uint16_t)((0x100U << 5) & 0xFFFFU);
    FilterConfig.FilterIdLow      = 0x0000U;
    FilterConfig.FilterMaskIdHigh = (uint16_t)((0x7F8U << 5) & 0xFFFFU);
    FilterConfig.FilterMaskIdLow  = 0x0000U;

    FilterConfig.FilterActivation = ENABLE;
    FilterConfig.FilterFIFOAssignment = CAN_FILTER_FIFO1;   // 与已使能的 CAN1_RX1 中断对应

    HAL_CAN_ConfigFilter(&hcan, &FilterConfig);
    /*2. 手动开启CAN*/
    HAL_CAN_Start(&hcan);
    /*3. 使能 FIFO1 接收中断：有帧挂起时进回调 */
    HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO1_MSG_PENDING);

    /*4. 清空软件接收状态*/
    rx_head = 0U;
    rx_tail = 0U;
    rx_drop_count = 0U;
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
 *
 * 注意：消费者只修改 rx_tail，不修改 rx_head；
 *       中断只修改 rx_head，不修改 rx_tail。
 *       这是典型的“单生产者-单消费者”环形队列，所以不需要加互斥锁。
 */
uint8_t Int_CAN_Pop(CAN_Msg_t *out) {
    if (rx_head == rx_tail) {                       /* 两个下标相等 = 队列空 */
        return 0;
    }

    *out = rx_queue[rx_tail];                       /* 先拷贝数据 */

    rx_tail = (uint16_t)((rx_tail + 1U) & (CAN_RX_QUEUE_LEN - 1U));  /* 再移动读指针 */
    return 1;
}

/**
 * @brief 软件队列满导致丢弃的新帧计数
 */
uint32_t Int_CAN_GetDropCount(void) {
    return rx_drop_count;
}

/**
 * @brief FIFO1 接收中断回调：把硬件 FIFO 里的报文尽快搬进软件队列
 * @param hcanx CAN句柄
 *
 * 为什么这里要用 while 循环：
 *   STM32 的 bxCAN 每个接收FIFO只有3级深度。
 *   如果一次中断只读1帧就退出，硬件FIFO里剩下的帧可能来不及读，
 *   新的CAN帧一到就会触发FIFO溢出（FOVR），一次丢多帧。
 *   所以一次进中断要尽量把硬件FIFO读空。
 *
 * 软件队列满时怎么办：
 *   为了不让硬件FIFO继续溢出，仍然要把当前帧从硬件FIFO读走，
 *   但读走后没有地方存，只能丢弃，同时 rx_drop_count++。
 *   最终 App_Recv_EndFrame() 会检查这个计数，发现丢帧就报错。
 */
void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef *hcanx) {
    CAN_RxHeaderTypeDef dummy_header;   /* 队列满时用来“读走并丢弃”的临时帧头 */
    uint8_t dummy_data[8];              /* 临时数据区 */

    while (HAL_CAN_GetRxFifoFillLevel(hcanx, CAN_RX_FIFO1) > 0U) {
        /* 计算“如果写入，下一帧会放到哪个位置” */
        uint16_t next = (uint16_t)((rx_head + 1U) & (CAN_RX_QUEUE_LEN - 1U));

        if (next == rx_tail) {                          /* 软件队列已满 */
            rx_drop_count++;                            /* 记录一次丢帧 */

            /*
             * 必须把当前硬件FIFO里的帧读走，否则硬件FIFO继续满，
             * 后面连续到来的帧会触发硬件FOVR，一次丢更多。
             * 读走后直接丢弃，最终由上层报错/重传。
             */
            (void)HAL_CAN_GetRxMessage(hcanx, CAN_RX_FIFO1,
                                       &dummy_header, dummy_data);
            continue;
        }

        /* 软件队列没满：从硬件FIFO读一帧，存到 rx_queue[rx_head] */
        CAN_Msg_t *slot = &rx_queue[rx_head];
        if (HAL_CAN_GetRxMessage(hcanx, CAN_RX_FIFO1,
                                 &slot->header, slot->data) != HAL_OK) {
            break;                                      /* 读取失败，退出循环 */
        }

        rx_head = next;                                 /* 数据写完后才更新写指针 */
    }
}
