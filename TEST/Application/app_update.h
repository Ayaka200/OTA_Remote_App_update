//
// Created by 23029 on 2026/9/12.
//

#ifndef TEST_APP_UPDATE_H
#define TEST_APP_UPDATE_H

#include "usart.h"
#include "Int_can.h"

extern uint8_t EXPECTED[4];                /* TEST发给Sendsoft的更新请求内容：AA BB AA BB */

#define UART_CMD_LEN            32                          /* 串口命令缓冲区大小 */
#define UPDATE_CMD_CAN_ID       0x000                       /* TEST请求更新的CAN ID，Sendsoft只收这个ID */

/*
 * APP_RX_BUF_SIZE：
 *   Sendsoft 每 4KB 发一个扇区CRC（0x103），
 *   所以TEST端也准备一个4KB缓存，先把当前扇区完整收下来，
 *   等0x103校验通过后再擦除/写入W25Q64。
 */
#define APP_RX_BUF_SIZE         4096U

#define W25Q_PAGE_SIZE          256U        /* W25Q64一页256字节，页写不能跨页 */
#define NEW_IMAGE_ADDR          0x200000UL  /* W25Q64中存放新固件的起始地址 */

#define BOOT_FLAG_CHECK_ADDR         0x08U   /* AT24C02 升级标志存储地址 */
#define BOOT_FLAG_REQ_UPDATE         0x0AU   /* 请求执行固件更新 */
#define BOOT_FLAG_NORMAL_BOOT        0x00U   /* 正常启动，无需更新 */
//校验密钥
#define BOOT_CHECK_KEY_ADDR          0x00U    /*升级标志位有效校验位地址*/
#define BOOT_CHECK_KEY               0xAAU    /*升级标志位有效*/

/*
 * ================= Sendsoft 发送协议 =================
 * 0x100：起始帧
 * 0x101：扇区序号（1字节）
 * 0x102：程序数据（1~8字节）
 * 0x103：当前4KB扇区的CRC32（4字节小端）
 * 0x104：整个固件总长度（4字节小端）
 * 0x105：整个固件的镜像CRC32（4字节小端）
 * 0x106：结束帧
 */
#define CAN_ID_START            0x100U
#define CAN_ID_SECTOR_NUM       0x101U
#define CAN_ID_DATA             0x102U
#define CAN_ID_SECTOR_CRC       0x103U
#define CAN_ID_TOTAL_LEN        0x104U
#define CAN_ID_IMAGE_CRC        0x105U
#define CAN_ID_END              0x106U

/*
 * 程序更新状态机：
 *   UPDATE_IDLE   ：空闲，运行原App，等待串口"new version"
 *   UPDATE_CMD_SR ：向Sendsoft发0x000请求更新
 *   UPDATE_RECV   ：接收并解析0x100~0x106
 *   UPDATE_END    ：所有CRC、长度校验通过
 *   UPDATE_ERROR  ：过程中出错或发生CAN丢帧
 */
typedef enum {
    UPDATE_IDLE = 0,
    UPDATE_CMD_SR,
    UPDATE_RECV,
    UPDATE_END,
    UPDATE_ERROR,
} UPDATE_STATUS;

void App_Update_Init(void);

void App_Update_SendCmd(void);

void App_Update_Work(void);

#endif //TEST_APP_UPDATE_H
