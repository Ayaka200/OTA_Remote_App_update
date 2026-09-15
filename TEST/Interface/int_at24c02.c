//
// Created by 23029 on 2026/9/5.
//

#include "int_at24c02.h"

#include "i2c.h"

/* 等待上一次写周期完成（内部函数） */
static void AT24C02_Wait_Write_Done(void) {
    uint32_t timeout = 1000;                     /* 最多等 1000 次 */
    while (HAL_I2C_IsDeviceReady(&hi2c1, AT24C02_ADDRESS, 1, 10) != HAL_OK) {
        if (--timeout == 0) return;              /* 超时直接退出，别死循环 */
    }
}

/**
 * @brief 向指定地址读取一个字节
 * @param Address 指定的地址
 * @return 返回的字节
 */
uint8_t Int_AT24C02_Read_Byte(uint16_t Address) {

    uint8_t data = 0;
    HAL_I2C_Mem_Read(&hi2c1, AT24C02_ADDRESS | 0x01, Address,I2C_MEMADD_SIZE_8BIT,&data,1,1000);
    return data;
}

/**
 * @brief 向指定地址写入一个字节
 * @param Address 要写入的地址
 * @param data 写入的数据
 */
void Int_AT24C02_Write_Byte(uint16_t Address,uint8_t data) {
    // 1.句柄, 2.从设备地址  3.地址  4.地址长度  5.数据  6.数据长度  7.超时时间
    HAL_I2C_Mem_Write(&hi2c1,AT24C02_ADDRESS,Address,I2C_MEMADD_SIZE_8BIT,&data,1,1000);

    AT24C02_Wait_Write_Done();
}

/**
 * @brief 读取多个字节
 * @param Address
 * @param data
 * @param len
 */
void Int_AT24C02_Read_Bytes(uint16_t Address,uint8_t *data,uint8_t len) {

    HAL_I2C_Mem_Read(&hi2c1, AT24C02_ADDRESS | 0x01, Address,I2C_MEMADD_SIZE_8BIT,data,len,1000);

}

/**
 * @brief 写入多个字节，一次只能写入一页，页大小为8字节,
 * @param Address
 * @param data
 * @param len
 */
void Int_AT24C02_Write_Bytes(uint16_t Address, uint8_t *data, uint8_t len) {

    while (len > 0) {
        // 当前页还剩多少可写
        uint8_t page_remain = AT24C02_PAGE_SIZE - (Address % AT24C02_PAGE_SIZE);
        // 本次写 min(len, 页剩余)
        uint8_t chunk = (len < page_remain) ? len : page_remain;

        if (HAL_I2C_Mem_Write(&hi2c1, AT24C02_ADDRESS, Address,
                              I2C_MEMADD_SIZE_8BIT, data, chunk, 1000) != HAL_OK) {
            return;   // 写失败就退出
                              }
        AT24C02_Wait_Write_Done();   // 等本次写周期完成

        Address += chunk;   // 地址前进
        data += chunk;      // 指针前进
        len -= chunk;       // 剩余长度减少
    }

}