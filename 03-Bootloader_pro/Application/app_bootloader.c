//
// Created by 23029 on 2026/9/2.
//

#include "app_bootloader.h"
#include "int_bootloader.h"
#include "main.h"
#include "usart.h"
#include "i2c.h"
#include <stdlib.h>

#include "int_factory.h"

uint8_t app_boot_update_status = BOOT_FLAG_NORMAL_BOOT;
uint8_t app_health_status = APP_FLAG_HEALTH;

/**
 * @brief 判断当前是否需要更新
 */
/* 修复 2：Check_Update 里检查返回值，AT24C02 失败就按"正常启动"处理 */
void App_Bootloader_Check_Update(void) {
    uint8_t data[2] = {0};
    if (HAL_I2C_Mem_Read(&hi2c1, AT24C02_ADDRESS, BOOT_CHECK_KEY_ADDR,
                         I2C_MEMADD_SIZE_8BIT, data, 2, 100) != HAL_OK) {
        app_boot_update_status = BOOT_FLAG_NORMAL_BOOT;   /* I2C 失败 → 正常启动 */
        return;
                         }
    if (data[0] == BOOT_CHECK_KEY) {
        app_boot_update_status = data[1];
    } else {
        /* 写失败也别卡死，忽略 */
        Int_AT24C02_Write_Byte(BOOT_CHECK_KEY_ADDR, BOOT_CHECK_KEY);
        Int_AT24C02_Write_Byte(BOOT_FLAG_CHECK_ADDR, BOOT_FLAG_NORMAL_BOOT);
    }
}

/**
 * @brief 判断A区程序是否需要重置
 */
void App_Bootloader_Check_Factory_Reset(void) {
    uint8_t data[2] = {0};
    if (HAL_I2C_Mem_Read(&hi2c1, AT24C02_ADDRESS, APP_HEALTH_KEY_ADDR,
                         I2C_MEMADD_SIZE_8BIT, data, 2, 100) != HAL_OK) {
        app_boot_update_status = APP_FLAG_HEALTH;   /* I2C 失败 → 正常启动 */
        return;
                         }
    if (data[0] == APP_HEALTH_KEY) {
        app_health_status = data[1];
    } else {
        /* 写失败也别卡死，忽略 */
        Int_AT24C02_Write_Byte(APP_HEALTH_KEY_ADDR, APP_HEALTH_KEY);
        Int_AT24C02_Write_Byte(APP_FLAG_HEALTH_ADDR, APP_FLAG_HEALTH);
    }
}

void App_Bootloader_Reset(void) {
    uint32_t t0 = HAL_GetTick();

    /* 3 秒手动恢复窗口：期间按下 KEY2 → 判 A 区不健康 */
    while (HAL_GetTick() - t0 < 3000) {
        if (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == GPIO_PIN_RESET) {
            HAL_Delay(20);   /* 消抖 */
            if (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == GPIO_PIN_RESET) {
                Int_AT24C02_Write_Byte(APP_HEALTH_KEY_ADDR, APP_HEALTH_KEY);
                Int_AT24C02_Write_Byte(APP_FLAG_HEALTH_ADDR, APP_FLAG_UNHEALTH);
                app_health_status = APP_FLAG_UNHEALTH;
                while (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == GPIO_PIN_RESET); /* 等松开 */
                break;
            }
        }
    }

    if (app_health_status == APP_FLAG_HEALTH) {

    }
    else if (app_health_status == APP_FLAG_UNHEALTH) {
        // A区程序出错 恢复出厂设置，传输默认程序
        // printf("Reseting bootloader\n");
        Int_Factory_Restore();
        Int_AT24C02_Write_Byte(IMG_SYNC_ADDR, IMG_SYNC_DONE);
    }
}

/**
 * @对A区程序进行更新
 */
void App_Bootloader_Update(void) {

    if (app_boot_update_status == BOOT_FLAG_REQ_UPDATE) {
        /* 1. 收 bin、写 App 区 */
        // printf("update bootloader\n");
        /* 2. CRC 校验通过 */
        /* 3. 到这才能置待同步 ↓ */
        if (Int_AT24C02_Read_Byte(IMG_SYNC_KEY_ADDR) == IMG_SYNC_KEY) {
            Int_AT24C02_Write_Byte(IMG_SYNC_ADDR, IMG_SYNC_NEED);
        }

    }
    else if (app_boot_update_status == BOOT_FLAG_NORMAL_BOOT) {
        //不需要更新
        // printf("normal bootloader\n");
    }
}

/**
 * @brief 执行跳转A区程序的操作
 */
void App_Bootloader_Jump(void) {

    Int_Bootloader_Jump_to_app();

}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == KEY2_Pin) {
        //手动恢复出厂设置
        //Int_AT24C02_Write_Byte(APP_FLAG_HEALTH_ADDR,APP_FLAG_UNHEALTH);
    }
}