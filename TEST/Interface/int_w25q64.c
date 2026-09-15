//
// Created by 23029 on 2026/9/3.
//

#include "int_w25q64.h"
#include "int_w25q64_ins.h"
/**
 * @brief 拉低片选，选中W25Q64
 */
static void Int_W25Q64_Start(void) {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
};

/**
 * @brief 释放片选
 */
static void Int_W25Q64_Stop(void) {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
}

/**
 * @brief 交换数据，更贴近SPI底层协议
 * @param byte 要写入的数据，读数据时填0xff
 * @return 返回的数据
 */
static uint8_t Int_W25Q64_SwapByte(uint8_t byte) {
    uint8_t recv_byte=0;
    HAL_SPI_TransmitReceive(&hspi1, &byte, &recv_byte, 1, 100);
    return recv_byte;
}

/**
 * @brief 写使能
 */
static void Int_W25Q64_WriteEnable(void) {
    Int_W25Q64_Start();
    Int_W25Q64_SwapByte(W25Q64_WRITE_ENABLE);
    Int_W25Q64_Stop();
}

/**
 * @brief 等待忙
 */
static void  Int_W25Q64_WaitBusy(void) {

    // 1.拉低片选
    Int_W25Q64_Start();

    // 2.读取状态寄存器
    Int_W25Q64_SwapByte(W25Q64_READ_STATUS_REGISTER_1);
    uint32_t tick = HAL_GetTick();
    while (Int_W25Q64_SwapByte(W25Q64_DUMMY_BYTE) & 0x01) {
        if (HAL_GetTick() - tick > 500) break;   /* 500ms 兜底 */
    }

    // 3.拉低片选
    Int_W25Q64_Stop();

}

/**
 * @brief 按扇区擦除数据
 * @param Addr 擦除扇区的地址 0x00000~0x7FFFFF
 */
void Int_W25Q64_EraseSector(uint32_t Addr) {

    // 1.拉低片选
    Int_W25Q64_Start();

    Int_W25Q64_WriteEnable();
    Int_W25Q64_Start();
    Int_W25Q64_SwapByte(W25Q64_SECTOR_ERASE_4KB);

    // 3.发送地址
    Int_W25Q64_SwapByte(Addr>>16);
    Int_W25Q64_SwapByte(Addr>>8);
    Int_W25Q64_SwapByte(Addr);

    // 4.拉高片选
    Int_W25Q64_Stop();

    // 5.等待忙
    Int_W25Q64_WaitBusy();

}

/**
 * @brief 按页编写
 * @param Addr 页编程的起始地址，范围：0x000000~0x7FFFFF
 * @param Send_Data_Buff 用于写入数据的数组
 * @param Count 要写入数据的数量，范围：0~256
 */
void Int_W25Q64_PageProgram(uint32_t Addr,uint8_t * Send_Data_Buff,uint16_t Count) {

    // 1.写使能
    Int_W25Q64_WriteEnable();

    // 2.拉蒂片选
    Int_W25Q64_Start();

    // 3.发送写命令
    Int_W25Q64_SwapByte(W25Q64_PAGE_PROGRAM);

    // 4.发送地址
    Int_W25Q64_SwapByte(Addr>>16);
    Int_W25Q64_SwapByte(Addr>>8);
    Int_W25Q64_SwapByte(Addr);

    // 5.发送数据
    for (uint16_t i=0;i<Count;i++) {
        Int_W25Q64_SwapByte(Send_Data_Buff[i]);
    }
    // 6.拉高片选
    Int_W25Q64_Stop();

    // 7.等待忙
    Int_W25Q64_WaitBusy();

}

/**
 * @读取芯片ID
 *
 * @param mf_id : MFID
 * @param id : ID
 */
void Int_W25Q64_Read_ID(uint8_t *mf_id,uint16_t *id) {

    // 1. 拉蒂片选
    Int_W25Q64_Start();

    // 2. 发送读取ID指令
    // Int_W25Q64_Write_Byte(W25Q64_JEDEC_ID);
    Int_W25Q64_SwapByte(W25Q64_JEDEC_ID);

    // 3.读取ID
    *mf_id = Int_W25Q64_SwapByte(W25Q64_DUMMY_BYTE);
    uint8_t High_ID = Int_W25Q64_SwapByte(W25Q64_DUMMY_BYTE);
    uint8_t Low_ID = Int_W25Q64_SwapByte(W25Q64_DUMMY_BYTE);
    *id = High_ID<<8 | Low_ID;

    // 4.释放片选
    Int_W25Q64_Stop();
}

/**
 * @brief 连续读取W25Q64
 * @param Addr 读取数据的起始地址，范围：0x000000~0x7FFFFF
 * @param Recv_Data_Buff 用于接收读取数据的数组，通过输出参数返回
 * @param Count 要读取数据的数量，范围：0~0x800000
 */
void Int_W25Q64_ReadData(uint32_t Addr,uint8_t * Recv_Data_Buff,uint32_t Count) {

    // 1.拉低片选
    Int_W25Q64_Start();

    // 2.发送读取指令
    Int_W25Q64_SwapByte(W25Q64_READ_DATA);

    // 3.发送地址
    Int_W25Q64_SwapByte(Addr>>16);
    Int_W25Q64_SwapByte(Addr>>8);
    Int_W25Q64_SwapByte(Addr);

    for (uint32_t i=0;i<Count;i++) {
        Recv_Data_Buff[i] = Int_W25Q64_SwapByte(W25Q64_DUMMY_BYTE);
    }

    Int_W25Q64_Stop();
}

uint8_t Int_W25Q64_ReadStatus(void) {
    Int_W25Q64_Start();
    Int_W25Q64_SwapByte(0x05);  /* 读状态寄存器1 */
    uint8_t sr = Int_W25Q64_SwapByte(0xFF);
    Int_W25Q64_Stop();
    return sr;
}
