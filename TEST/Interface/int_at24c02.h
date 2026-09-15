//
// Created by 23029 on 2026/9/5.
//

#ifndef INC_03_BOOTLOADER_PRO_AT24C02_H
#define INC_03_BOOTLOADER_PRO_AT24C02_H

#include  "main.h"

#define AT24C02_ADDRESS 0xA0
#define AT24C02_DATA_SIZE 8
#define AT24C02_PAGE_SIZE 8


uint8_t Int_AT24C02_Read_Byte(uint16_t Address);

void Int_AT24C02_Write_Byte(uint16_t Address,uint8_t data);

void Int_AT24C02_Read_Bytes(uint16_t Address,uint8_t *data,uint8_t len);

void Int_AT24C02_Write_Bytes(uint16_t Address,uint8_t *data,uint8_t len);

#endif //INC_03_BOOTLOADER_PRO_AT24C02_H