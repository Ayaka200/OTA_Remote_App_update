//
// Created by 23029 on 2026/9/6.
//

#ifndef INC_03_BOOTLOADER_PRO_INT_FACTORY_H
#define INC_03_BOOTLOADER_PRO_INT_FACTORY_H
#include "main.h"
#define FACTORY_IMG_BASE_ADDR       0x000000UL          //备份程序的起始地址
#define FACTORY_IMG_HEAD_LEN        256                  //头部用于校验
#define FACTORY_IMG_MAX_LEN         (48UL * 1024U)      //备份程序区的大小
#define FACTORY_IMG_SECTOR_LEN      12                  //备份程序占用的扇区数量

typedef struct {
    uint32_t magic;
    uint32_t length;
    uint32_t crc32;
    uint32_t reserved;
} Factory_Image_Header;                             // 数据头部结构体，用于校验备份程序的健康性

void Int_Factory_Init(void);

uint8_t Int_Factory_Image_Append(uint8_t *data_buf,uint32_t len);

uint8_t Int_Factory_Commit(uint32_t total_len);

uint8_t Int_Factory_Check_Image(void);

uint8_t Int_Factory_Restore(void);

uint8_t Int_Factory_Backup_From_App(uint32_t len);

// void Test_Factory_Backup(void);
//
// void Test_Factory_Restore(void);
//
// void Test_Factory_Real(void);

#endif //INC_03_BOOTLOADER_PRO_INT_FACTORY_H
