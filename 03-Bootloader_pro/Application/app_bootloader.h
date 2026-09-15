//
// Created by 23029 on 2026/9/2.
//

#ifndef INC_02_BOOTLOADER_APP_BOOTLOADER_H
#define INC_02_BOOTLOADER_APP_BOOTLOADER_H

#include "main.h"
#include "app_bootloader.h"
#include "int_at24c02.h"


//更新状态
#define BOOT_FLAG_CHECK_ADDR         0x08U   /* AT24C02 升级标志存储地址 */
#define BOOT_FLAG_REQ_UPDATE         0x0AU   /* 请求执行固件更新 */
#define BOOT_FLAG_NORMAL_BOOT        0x00U   /* 正常启动，无需更新 */
//校验密钥
#define BOOT_CHECK_KEY_ADDR          0x00U    /*升级标志位有效校验位地址*/
#define BOOT_CHECK_KEY               0xAAU    /*升级标志位有效*/

//A区程序健康状态
#define APP_FLAG_HEALTH_ADDR         0x18U    /*A区程序正常运行标志位存储地址*/
#define APP_FLAG_HEALTH              0x0BU    /*A区程序正常进行*/
#define APP_FLAG_UNHEALTH            0x00U    /*A区程序未能正常运行*/
//校验密钥
#define APP_HEALTH_KEY_ADDR          0x10U    /*A区程序健康标志位的校验位地址*/
#define APP_HEALTH_KEY               0xBBU    /*A区程序健康标志位有效*/

//备份待同步状态
#define IMG_SYNC_ADDR                0x28U     /*镜像区程序待同步标志位存储地址*/
#define IMG_SYNC_NEED                0x0CU     /*镜像区程序需要同步*/
#define IMG_SYNC_DONE                0xC0U     /*镜像区陈鼓型不需要同步*/
//校验密钥
#define IMG_SYNC_KEY_ADDR            0x20U      /*镜像区同步标志位校验位地址*/
#define IMG_SYNC_KEY                 0xCCU      /*镜像区同步标志位校验位*/


void App_Bootloader_AT24C02_Init(void);

void App_Bootloader_Check_Update(void);

void App_Bootloader_Check_Factory_Reset(void);

void App_Bootloader_Update(void);

void App_Bootloader_Jump(void);

void App_Bootloader_Reset(void);

#endif //INC_02_BOOTLOADER_APP_BO