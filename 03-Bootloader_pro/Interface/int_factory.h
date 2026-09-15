//
// Created by 23029 on 2026/9/6.
//

#ifndef INC_03_BOOTLOADER_PRO_INT_FACTORY_H
#define INC_03_BOOTLOADER_PRO_INT_FACTORY_H
#include "main.h"
#define FACTORY_IMG_BASE_ADDR       0x000000UL          //备份程序的起始地址
#define IMG_HEAD_LEN        256                  //出厂备份镜像：头部固定占 256 字节，数据紧跟其后

/*
 * ================= W25Q64 新固件布局（必须和 TEST/Application/app_update.h 完全一致） =================
 *   0x200000 ~ 0x200FFF : 头部区，独占 1 个 4KB 扇区
 *   0x201000 ~          : 固件数据区，按 4KB 对齐
 * reserved 字段写入 NEW_IMAGE_LAYOUT_TAG，用来识别"老布局"留下的镜像，避免误烧。
 */
#define NEW_IMAGE_ADDR          0x200000UL  /* 新固件头部地址，独占第 1 个 4KB 扇区 */
#define NEW_IMAGE_DATA_ADDR     (NEW_IMAGE_ADDR + 0x1000UL) /* 固件数据起始地址 */
#define NEW_IMAGE_LAYOUT_TAG    0x00000002UL /* 布局标记：数据起始 = NEW_IMAGE_ADDR + 4096 */

#define FACTORY_IMG_MAX_LEN         (48UL * 1024U)      //备份程序区的大小
#define FACTORY_IMG_SECTOR_LEN      12                  //备份程序占用的扇区数量

typedef struct {
    uint32_t magic;
    uint32_t length;
    uint32_t crc32;
    uint32_t reserved;
} Image_Header;                             // 数据头部结构体，用于校验备份程序的健康性

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
