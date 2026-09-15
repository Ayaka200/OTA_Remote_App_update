//
// Created by 23029 on 2026/9/2.
//

#include "app_bootloader.h"
#include "int_bootloader.h"
#include "main.h"
#include "usart.h"
#include "i2c.h"
#include <stdlib.h>
#include <string.h>

#include "int_factory.h"
#include "int_w25q64.h"

/* W25Q64 连续读/写的分块大小：正好一个页 256 字节 */
#define W25Q_READ_CHUNK   256U

uint8_t app_boot_update_status = BOOT_FLAG_NORMAL_BOOT;
uint8_t app_health_status = APP_FLAG_HEALTH;

void App_Bootloader_AT24C02_Init(void) {
    /* 校验密钥不存在就写入默认值 */
    if (Int_AT24C02_Read_Byte(BOOT_CHECK_KEY_ADDR) != BOOT_CHECK_KEY) {
        Int_AT24C02_Write_Byte(BOOT_CHECK_KEY_ADDR, BOOT_CHECK_KEY);

    }
    /* APP 健康标志同理 */
    if (Int_AT24C02_Read_Byte(APP_HEALTH_KEY_ADDR) != APP_HEALTH_KEY) {
        Int_AT24C02_Write_Byte(APP_HEALTH_KEY_ADDR, APP_HEALTH_KEY);

    }
}


/**
 * @brief 判断当前是否需要更新
 */
/* 修复 2：Check_Update 里检查返回值，AT24C02 失败就按"正常启动"处理 */
void App_Bootloader_Check_Update(void) {
    uint8_t key = Int_AT24C02_Read_Byte(BOOT_CHECK_KEY_ADDR);
    if (key == BOOT_CHECK_KEY) {
        app_boot_update_status = Int_AT24C02_Read_Byte(BOOT_FLAG_CHECK_ADDR);
    } else {
        Int_AT24C02_Write_Byte(BOOT_CHECK_KEY_ADDR, BOOT_CHECK_KEY);
        Int_AT24C02_Write_Byte(BOOT_FLAG_CHECK_ADDR, BOOT_FLAG_NORMAL_BOOT);
        app_boot_update_status = BOOT_FLAG_NORMAL_BOOT;
    }
}


/**
 * @brief 判断A区程序是否需要重置
 *
 * 注意：健康"校验位"在 APP_HEALTH_KEY_ADDR(0x10)，
 *       健康"标志位"在 APP_FLAG_HEALTH_ADDR(0x18)，两者并不相邻，
 *       所以必须分别读，不能像以前那样从 0x10 连读 2 字节（读到的是 0x11）。
 */
void App_Bootloader_Check_Factory_Reset(void) {
    if (Int_AT24C02_Read_Byte(APP_HEALTH_KEY_ADDR) == APP_HEALTH_KEY) {
        app_health_status = Int_AT24C02_Read_Byte(APP_FLAG_HEALTH_ADDR);
    } else {
        /* 写失败也别卡死，忽略 */
        Int_AT24C02_Write_Byte(APP_HEALTH_KEY_ADDR, APP_HEALTH_KEY);
        Int_AT24C02_Write_Byte(APP_FLAG_HEALTH_ADDR, APP_FLAG_HEALTH);
        app_health_status = APP_FLAG_HEALTH;
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
 * @brief 与 TEST / Sendsoft 完全一致的 CRC32 逐字节更新
 */
static uint32_t App_Boot_CRC_Calculate(uint8_t byte, uint32_t crc)
{
    crc ^= (uint32_t)byte << 24;
    for (uint8_t i = 0U; i < 8U; i++) {
        crc = (crc & 0x80000000UL) ? (crc << 1) ^ 0x04C11DB7UL : (crc << 1);
    }
    return crc;
}

/**
 * @brief 把 W25Q64 里的新固件整体读一遍算 CRC32
 *
 * 这一步必须在擦除 A 区之前做：
 * 镜像坏了就直接放弃，A 区里的老程序还能继续跑；
 * 否则擦掉老程序、烧进坏镜像，板子就只能靠 SWD 救了。
 *
 * @return 1=镜像完好  0=镜像损坏
 */
static uint8_t App_Boot_Check_New_Image(uint32_t length, uint32_t expect_crc)
{
    static uint8_t buf[W25Q_READ_CHUNK];   /* 静态，别压栈 */
    uint32_t crc = 0xFFFFFFFFUL;

    for (uint32_t offset = 0U; offset < length; offset += W25Q_READ_CHUNK) {
        uint32_t chunk = (length - offset) > W25Q_READ_CHUNK ? W25Q_READ_CHUNK : (length - offset);
        Int_W25Q64_ReadData(NEW_IMAGE_DATA_ADDR + offset, buf, chunk);
        for (uint32_t i = 0U; i < chunk; i++) {
            crc = App_Boot_CRC_Calculate(buf[i], crc);
        }
    }

    printf("w25q crc=%08lX expect=%08lX\r\n",
           (unsigned long)(~crc), (unsigned long)expect_crc);
    return ((~crc) == expect_crc) ? 1U : 0U;
}

/**
 * @brief 回读内部 Flash，重新算 CRC32，确认真的烧进去了
 * @return 1=写入正确  0=写入有误
 */
static uint8_t App_Boot_Verify_Flash(uint32_t length, uint32_t expect_crc)
{
    const uint8_t *p = (const uint8_t *)APP_START_ADDR;
    uint32_t crc = 0xFFFFFFFFUL;

    for (uint32_t i = 0U; i < length; i++) {
        crc = App_Boot_CRC_Calculate(p[i], crc);
    }

    printf("flash crc=%08lX expect=%08lX\r\n",
           (unsigned long)(~crc), (unsigned long)expect_crc);
    return ((~crc) == expect_crc) ? 1U : 0U;
}

/**
 * @对A区程序进行更新
 */
void App_Bootloader_Update(void) {
    printf("update_status=%d\r\n", app_boot_update_status);

    if (app_boot_update_status != BOOT_FLAG_REQ_UPDATE) {
        return;   /* 正常启动，不需要更新 */
    }

    Image_Header hdr;
    uint8_t new_img_buff[W25Q_READ_CHUNK];
    uint32_t app_size = APP_END_ADDR - APP_START_ADDR;

    /* 1. 读头部并校验 */
    Int_W25Q64_ReadData(NEW_IMAGE_ADDR, (uint8_t *)&hdr, sizeof(hdr));
    printf("hdr magic=%08lX len=%lu tag=%08lX crc=%08lX\r\n",
           (unsigned long)hdr.magic, (unsigned long)hdr.length,
           (unsigned long)hdr.reserved, (unsigned long)hdr.crc32);

    if (hdr.magic != 0x55AA55AAUL) {
        printf("bad magic, keep old app\r\n");
        return;  /* 没有有效固件 */
    }
    if (hdr.reserved != NEW_IMAGE_LAYOUT_TAG) {
        printf("bad layout tag, keep old app\r\n");
        return;  /* 老布局或来路不明的镜像，绝不烧 */
    }
    if (hdr.length == 0U || hdr.length > app_size) {
        printf("bad length, keep old app\r\n");
        return;  /* 长度不合法 */
    }

    /* 2. 先整包校验 W25Q64 里的新固件，坏了直接放弃（老程序保持不动） */
    if (App_Boot_Check_New_Image(hdr.length, hdr.crc32) == 0U) {
        printf("new image crc bad, keep old app\r\n");
        return;
    }

    /* 3. 擦除 APP 区 */
    Int_Bootloader_Erase_Flash(APP_START_ADDR, (uint16_t)(app_size / 1024U));

    /* 4. 从 W25Q64 读固件，写内部 Flash */
    HAL_FLASH_Unlock();
    for (uint32_t offset = 0U; offset < hdr.length; offset += W25Q_READ_CHUNK) {
        uint32_t chunk = (hdr.length - offset) > W25Q_READ_CHUNK ? W25Q_READ_CHUNK : (hdr.length - offset);
        Int_W25Q64_ReadData(NEW_IMAGE_DATA_ADDR + offset, new_img_buff, chunk);
        for (uint32_t i = 0U; i < chunk; i += 2U) {
            uint16_t halfword = 0xFFFFU;
            uint16_t n = (chunk - i >= 2U) ? 2U : (uint16_t)(chunk - i);
            memcpy(&halfword, new_img_buff + i, n);
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                                  APP_START_ADDR + offset + i, halfword) != HAL_OK) {
                HAL_FLASH_Lock();
                printf("flash program fail @%08lX\r\n", (unsigned long)(APP_START_ADDR + offset + i));
                return;   /* 升级标志保留，下次复位重试 */
            }
        }
    }
    HAL_FLASH_Lock();

    /* 5. 回读内部 Flash 校验，通过了才算升级成功 */
    if (App_Boot_Verify_Flash(hdr.length, hdr.crc32) == 0U) {
        printf("flash verify fail\r\n");
        return;   /* 升级标志保留，下次复位重试 */
    }
    printf("write ok, jump now\r\n");

    /* 6. 置待同步、清升级标志 */
    if (Int_AT24C02_Read_Byte(IMG_SYNC_KEY_ADDR) == IMG_SYNC_KEY) {
        Int_AT24C02_Write_Byte(IMG_SYNC_ADDR, IMG_SYNC_NEED);
    }
    if (Int_AT24C02_Read_Byte(BOOT_CHECK_KEY_ADDR) == BOOT_CHECK_KEY) {
        Int_AT24C02_Write_Byte(BOOT_FLAG_CHECK_ADDR, BOOT_FLAG_NORMAL_BOOT);
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
