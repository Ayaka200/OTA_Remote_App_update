//
// Created by 23029 on 2026/9/3.
//

#ifndef INC_03_BOOTLOADER_PRO_INT_W25Q64_H
#define INC_03_BOOTLOADER_PRO_INT_W25Q64_H

#include "main.h"
#include "spi.h"

void Int_W25Q64_EraseSector(uint32_t Addr);

void Int_W25Q64_Read_ID(uint8_t *mf_id,uint16_t *id);

void Int_W25Q64_PageProgram(uint32_t Addr,uint8_t * Send_Data_Buff,uint16_t Count);

void Int_W25Q64_ReadData(uint32_t Addr,uint8_t * Recv_Data_Buff,uint32_t Count);

uint8_t Int_W25Q64_ReadStatus(void);

#endif //INC_03_BOOTLOADER_PRO_INT_W25Q64_H
