//
// Created by 23029 on 2026/8/31.
//

#include "int_bootloader.h"

#include <sys/types.h>

#include "main.h"
#include "usart.h"

/* 环形缓冲区大小：必须是 2 的幂，且要大于 BOOTLOADER_UART_RECV_BUFF_LEN + 1 */
#define UART_RING_SIZE  2048

static uint8_t  uart_ring[UART_RING_SIZE];
static volatile uint16_t ring_head  = 0;   // 写入位置
static volatile uint16_t ring_tail  = 0;   // 读取位置
static volatile uint16_t ring_count = 0;   // 当前缓冲区内字节数


uint8_t uart_recv_buff[BOOTLOADER_UART_RECV_BUFF_LEN]={0};
uint16_t uart_recv_len=0;
uint16_t uart_recv_full_len=0;
uint32_t flash_write_offset=0;      // 当前写入程序的偏移量
 // uint32_t last_recv_time=0;

static void Int_FLASH_Erase(uint16_t byte_count) {
    /*4.擦除判断：只检查本次将要写入的 write_bytes 个字节*/
    uint8_t is_erase = 0;
    uint32_t page_addr=0; //记录当前的页地址

    //4.1遍历需要写入的地址长度为当前接收的数据长度如果全部内容都是0xff 则说明已经擦除过了
    for (uint16_t i=0;i<byte_count ;i++) {
        //读取每一位的值，判断是否都为0xFF
        uint8_t data = *(volatile uint8_t *)(APP_START_ADDR+i+flash_write_offset);
        if (data != 0xff) {
            is_erase = 1;
            page_addr=(APP_START_ADDR+i+flash_write_offset)-
                (APP_START_ADDR+i+flash_write_offset)%FLASH_PAGE_SIZE;
            break;
        }
    }
    //4.2 擦除需要擦除的页
    if (is_erase) {
        FLASH_EraseInitTypeDef EraseInit;
        EraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
        EraseInit.Banks = FLASH_BANK_1;
        EraseInit.PageAddress = page_addr;
        EraseInit.NbPages = 1;
        uint32_t page_error = 0;
        HAL_FLASHEx_Erase(&EraseInit, &page_error);
    }
}

/**
 * @brief 写入环形缓存区
 * @param byte
 */
static volatile uint8_t ring_overflow = 0;   /* 缓冲区溢出标志 */

static void ring_write(uint8_t byte) {
    if (ring_count >= UART_RING_SIZE) {
        ring_overflow = 1;   /* 置溢出标志，丢弃新数据，不死循环 */
        return;
    }
    uart_ring[ring_head]=byte;
    ring_head = (ring_head+1)&(UART_RING_SIZE-1);
    ring_count++;
}

/**
 * @brief 读取环形缓冲区
 * @return
 */
static uint8_t ring_read(void) {
    uint8_t byte = uart_ring[ring_tail];
    ring_tail = (ring_tail+1)&(UART_RING_SIZE-1);
    ring_count--;
    return byte;
}

static void Int_FLASH_Write(void) {
    while (ring_count >= 2) {
        uint8_t  lo = ring_read();
        uint8_t  hi = ring_read();
        uint16_t data_16 = (uint16_t)(lo | (hi << 8));

        HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                          APP_START_ADDR + flash_write_offset,
                          data_16);

        flash_write_offset += 2;
    }
}

/**
 * @brief 收尾：把环形缓冲区里剩下的、凑不满一个半字的字节也写进 Flash
 *
 * STM32F1 只能按 16 位编程，所以最后 1 个字节要配上 0xFF 一起写。
 * 镜像是奇数字节时（.bin 长度很常见），以前这个字节会一直留在环形缓冲区里，
 * Flash 里对应位置还是 0xFF，发出去的固件最后一个字节就是错的。
 */
void Int_FLASH_FlushTail(void) {
    if (ring_count == 0U) {
        return;   /* 偶数长度：没有尾巴，直接返回 */
    }

    HAL_FLASH_Unlock();
    Int_FLASH_Write();     /* 先按半字把成对的写完 */

    if (ring_count != 0U) {
        uint8_t last = ring_read();
        uint32_t addr = APP_START_ADDR + flash_write_offset;
        uint16_t halfword;

        if ((flash_write_offset & 1U) == 0U) {
            /* 落在半字的低字节：高字节保持擦除态的 0xFF */
            halfword = (uint16_t)(0xFF00U | (uint16_t)last);
        } else {
            /* 落在半字的高字节：低字节是刚写进去的那个字节，读回来一起重写 */
            uint8_t prev = *(volatile uint8_t *)(addr - 1U);
            halfword = (uint16_t)((uint16_t)prev | ((uint16_t)last << 8));
            addr -= 1U;
        }

        HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr, halfword);
        flash_write_offset++;
    }
    HAL_FLASH_Lock();
}


/* ================= IT 版本：环形缓冲区 + Flash 写入 ================= */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart-> Instance == USART1) {
        //记录接收到数据时的系统时间
         // last_recv_time=HAL_GetTick();
        // 保存接收的数据长度
        uart_recv_len=Size;
        uart_recv_full_len+=uart_recv_len;

        /* 1. 新收到的数据全部放入环形缓冲区 */
        for (uint16_t i=0;i<uart_recv_len;i++) {
            ring_write(uart_recv_buff[i]);
        }
        /* 2. 本次能凑出多少个完整字节：向下取偶数 */
        uint16_t write_bytes= ring_count & ~1u;

        if (write_bytes == 0) {
            /* 没有完整半字可写，重新启动接收 */
            __HAL_UART_CLEAR_OREFLAG(&huart1);
            __HAL_UART_CLEAR_IDLEFLAG(&huart1);
            HAL_UARTEx_ReceiveToIdle_IT(&huart1, uart_recv_buff,
                                        BOOTLOADER_UART_RECV_BUFF_LEN);
            return;
        }


        // 将接收到的数据写入到Flash当中
        /*3. 解锁FLASH*/
        HAL_FLASH_Unlock();
        //4 擦除
        Int_FLASH_Erase(write_bytes);

        //5 使用16位的方法写入FLASH
        /* 每次取 2 个字节组成 16 位数据写入 Flash */
        Int_FLASH_Write();

        /*6.锁定FLASH*/
        HAL_FLASH_Lock();

        // 写入完成后清空接受区
        memset(uart_recv_buff,0,BOOTLOADER_UART_RECV_BUFF_LEN);

        // 做下一次接收准备
        __HAL_UART_CLEAR_OREFLAG(&huart1);
        __HAL_UART_CLEAR_IDLEFLAG(&huart1);
        HAL_UARTEx_ReceiveToIdle_IT(&huart1, uart_recv_buff, BOOTLOADER_UART_RECV_BUFF_LEN);
    }
}

void Int_Bootloader_Recv_App(void) {
    /*0. 把写入偏移和环形缓冲区清干净
         否则同一轮里第二次接收会接着上次的偏移往后面写，镜像整体错位*/
    flash_write_offset = 0;
    ring_head = 0;
    ring_tail = 0;
    ring_count = 0;
    ring_overflow = 0;
    memset(uart_recv_buff, 0, BOOTLOADER_UART_RECV_BUFF_LEN);

    /*1. 先中止可能残留的阻塞接收，重置 UART 状态*/
    HAL_UART_AbortReceive(&huart1);
    /*2. 清空标志位*/
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    __HAL_UART_CLEAR_IDLEFLAG(&huart1);
    /*3. 启动中断接收*/
    HAL_UARTEx_ReceiveToIdle_IT(&huart1, uart_recv_buff, BOOTLOADER_UART_RECV_BUFF_LEN);
}
/* ================= 原 IT 版本结束 ================= */

/**
 * @brief 提前擦除FLASH指定地址的页数
 * @param Page_Addr 要擦出的地址
 * @param Pages  擦出的页数
 */
void Int_Bootloader_Erase_Flash(uint32_t Page_Addr,uint16_t Pages) {

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef EraseInit;
    EraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    EraseInit.Banks = FLASH_BANK_1;
    EraseInit.PageAddress = Page_Addr;
    EraseInit.NbPages = Pages;
    uint32_t page_error = 0;
    HAL_FLASHEx_Erase(&EraseInit, &page_error);

    HAL_FLASH_Lock();
}
