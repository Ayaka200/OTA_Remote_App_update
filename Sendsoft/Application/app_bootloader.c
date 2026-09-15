//
// Created by 23029 on 2026/9/2.
//

#include "app_bootloader.h"
#include "int_bootloader.h"
#include "main.h"
#include "usart.h"
#include <stdlib.h>

uint8_t app_recv_buff[64]={0};
uint16_t app_recv_len = 0;
uint32_t app_recv_total_len = 0;
 uint32_t last_recv_time=0;             //记录最后一次接收到数据的时间
extern uint16_t uart_recv_full_len;         //记录数据长度

Bootloader_Status bootloader_status=BOOTLOADER_INIT;    //记录当前状态

static uint8_t recv_started = 0;                        //标记是否已启动固件数据接收

/**
 * @brief 初始化Bootloader
 */
void App_Bootloader_Init(void) {
    printf("Bootloader Start\r\n");
    printf("Waiting User Send Data\r\n");
    printf("Please Send start:len to start\r\n");
    bootloader_status=BOOTLOADER_RUN;

}

/**
 * @brief 等待用户传输确认，开始传输
 */
void App_Bootloader_Run(void) {
    //使用非中断方式接收用户操作，区分硬件层程序
    //挂起接收，知道buff收满，或者接收到空闲帧
    HAL_UARTEx_ReceiveToIdle(&huart1,app_recv_buff,64,&app_recv_len,1000);
    if (app_recv_len > 0) {
        //判断数据格式
        char * volatile start_str = strstr((char *)app_recv_buff,"start:");
        if (start_str != NULL)
        {
            //保存接收的数据长度
            app_recv_total_len=atoi((char *)start_str+6);

            if (app_recv_total_len>0) {
                printf("app_len:%lu\r\n",(unsigned long)app_recv_total_len);
                //修改状态
                bootloader_status=BOOTLOADER_PRE;
            }
            else {
                printf("Len Error\r\n");
            }

        }
        else
        {
            printf("data error\r\n");
        }
    }
}

/**
 * @brief 接收数据
 */
void App_Bootloader_Recv_Data(void) {
    //只在第一次进入时启动一次接收，避免主循环反复重启
    static uint32_t prev_len = 0; // 记录上一次的累计长度

    if (!recv_started) {
        Int_Bootloader_Recv_App();
        recv_started = 1;
        last_recv_time = HAL_GetTick();   // 兜底：启动时也初始化，防秒进
        prev_len = 0;

    }
    // 关键：有"新数据到达"才刷新最后活动时间
    if (uart_recv_full_len != prev_len) {
        prev_len = uart_recv_full_len;
        last_recv_time = HAL_GetTick();     // 每收到新字节都重置空闲计时
    }
    //空闲两秒
    if ((uart_recv_full_len > 0) && (HAL_GetTick() - last_recv_time) >= 5000) {
        printf("Recv Data OK, len=%d\r\n", uart_recv_full_len);
        Int_FLASH_FlushTail();   /* 镜像长度是奇数时，把最后 1 个字节补写进去 */
        bootloader_status = BOOTLOADER_CHECK_DATA;
    }


}

/**
 * @brief 传输完成后，校验数据
 */
uint8_t App_Bootloader_Check_Data(void) {
    if (app_recv_total_len == uart_recv_full_len) {
        //校验通过
        printf("check ok\r\n");
        bootloader_status=BOOTLOADER_DONE;
        return 1;
    }
    else {
        //校验失败
        return 0;
    }
}




/**
 * @brief 用于main函数中的while调用
 */
void App_Bootloader_Work(void) {

    switch (bootloader_status) {
        case BOOTLOADER_INIT:
            App_Bootloader_Init();
            break;
        case BOOTLOADER_RUN:
            //开始接收用户操作
            App_Bootloader_Run();
            break;
        case BOOTLOADER_PRE: {
            /*
             * 关键修复：必须把整个镜像要占用的页全部擦掉。
             *
             * 以前固定只擦 10 页(10KB)，而镜像有 19KB 左右，
             * 0x8008000 之后的页还残留着上一次的镜像数据。
             * STM32F1 的内部 Flash 只能把 bit 由 1 写 0，
             * 对着没擦干净的页写新数据 = 新数据 & 旧数据 → 存进来的镜像被写坏，
             * 而后面所有 CRC 都是对"读回来这份坏数据"算的，一路校验都能过，
             * 最后把坏固件烧进 A 区，现象就是"复位后程序没更新/起不来"。
             */
            uint32_t pages = (app_recv_total_len + 1023UL) / 1024UL;
            if (pages > ((APP_END_ADDR - APP_START_ADDR) / 1024UL)) {
                printf("image too big:%lu\r\n", (unsigned long)app_recv_total_len);
                bootloader_status = BOOTLOADER_INIT;
                break;
            }
            Int_Bootloader_Erase_Flash(APP_START_ADDR, (uint16_t)pages);
            printf("erase %lu pages\r\n", (unsigned long)pages);
            recv_started = 0;
            uart_recv_full_len = 0;
            last_recv_time = HAL_GetTick();
            bootloader_status = BOOTLOADER_RECV_DATA;
            break;
        }
        case BOOTLOADER_RECV_DATA:
            //接收数据
            App_Bootloader_Recv_Data();
            break;
        case BOOTLOADER_CHECK_DATA:
            uint8_t res = App_Bootloader_Check_Data();
            if (res==0) {
                printf("check failed,system reset...\r\n");
                bootloader_status=BOOTLOADER_INIT;
                // NVIC_SystemReset();
            }

            break;

        default:
            break;
    }
}

