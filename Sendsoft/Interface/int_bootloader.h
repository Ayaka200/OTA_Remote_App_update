//
// Created by 23029 on 2026/8/31.
//

#ifndef INC_02_BOOTLOADER_INT_BOOTLOADER_H
#define INC_02_BOOTLOADER_INT_BOOTLOADER_H

#include "main.h"
#define  BOOTLOADER_UART_RECV_BUFF_LEN 512  //接收缓存区大小

//FLASH起始地址为0x8000000，假设B区大小为20K，则A区起始地址为0x8000000+0x005000
#define APP_START_ADDR   0x8005000
#define APP_END_ADDR     0x8010000UL
#define RAM_BASE         0x20000000UL
#define RAM_END          0x20005000UL   /* 20KB RAM 顶端 */

/**
 * @brief 初始化,串口接收 => 接收A程序（DMA 版）
 */
void Int_Bootloader_DMA_Init(void);

/**
 * @brief 初始化,串口接收 => 接收A程序（IT 版，已被 DMA 版替代，保留备用）
 */
void Int_Bootloader_Recv_App(void);

/**
 * @brief 收尾：把环形缓冲区里剩下的 1 个字节（奇数长度镜像）也写进 Flash
 */
void Int_FLASH_FlushTail(void);

/**
 *@brief 跳转程序至APP
 *retrun 0: 跳转成功,1: 跳转失败
 */
uint8_t Int_Bootloader_Jump_to_app(void);

/**
 * @brief 提前擦除FLASH指定地址的页数
 * @param Page_Addr 要擦出的地址
 * @param Pages  擦出的页数
 */
void Int_Bootloader_Erase_Flash(uint32_t Page_Addr,uint16_t Pages);

#endif //INC_02_BOOTLOADER_INT_BOOTLOADER_H
