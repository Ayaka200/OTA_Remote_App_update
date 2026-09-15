//
// Created by 23029 on 2026/9/2.
//

#ifndef INC_02_BOOTLOADER_APP_BOOTLOADER_H
#define INC_02_BOOTLOADER_APP_BOOTLOADER_H

#include "main.h"
#include "app_bootloader.h"

typedef enum {
    BOOTLOADER_INIT,
    BOOTLOADER_RUN,
    BOOTLOADER_PRE,
    BOOTLOADER_RECV_DATA,
    BOOTLOADER_CHECK_DATA,
    BOOTLOADER_DONE,
    BOOTLOADER_NULL
}Bootloader_Status;

/**
 * @brief 初始化Bootloader
 */
void App_Bootloader_Init(void);

/**
 * @brief 等待用户传输确认，开始传输
 */
void App_Bootloader_Run(void);

/**
 * @brief 接收数据
 */
void App_Bootloader_Recv_Data(void);

uint8_t App_Bootloader_Check_Data(void);

/**
 * @brief 用于main函数中的while调用
 */
void App_Bootloader_Work(void);

#endif //INC_02_BOOTLOADER_APP_BO