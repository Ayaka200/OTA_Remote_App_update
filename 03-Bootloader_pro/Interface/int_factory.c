//
// Created by 23029 on 2026/9/6.
//

#include "int_factory.h"

#include <stdint.h>
#include "int_bootloader.h"
#include "int_w25q64.h"

#define FACTORY_IMG_MAGIC 0x55AA55AAUL

static uint32_t img_offset = 0;        //写入数据的偏移量
static uint32_t img_crc=0;             //CRC累加
/**
 * @brief  CRC32，标准多项式 0x04C11DB7，逐位实现（48KB 约 50ms，够用）
 * @param Byte 要计算的字节
 * @param crc  CRC值
 * @return CRC32位计算的结果
 */
static uint32_t Factory_CRC_Calculate(uint8_t Byte,uint32_t crc) {

    crc ^= (uint32_t)Byte << 24;
    for (int i = 0; i < 8; i++)
        crc = (crc & 0x80000000UL) ? (crc << 1) ^ 0x04C11DB7UL : (crc << 1);
    return crc;
}

/**
 * @brief 清空Flash内的数据，为备份程序提供空间
 */
void Int_Factory_Init(void) {
    for (uint8_t i = 0; i < FACTORY_IMG_SECTOR_LEN; i++) {

        Int_W25Q64_EraseSector(FACTORY_IMG_BASE_ADDR + i * 4096);
    }
    img_offset = 0;
    img_crc=0xFFFFFFFFUL;
}

/**
 *@brief 页写入(后续加入CRC校验)
 * @param data_buf
 * @param len
 * @return
 */
uint8_t Int_Factory_Image_Append(uint8_t *data_buf,uint32_t len) {

    if (len + img_offset > FACTORY_IMG_MAX_LEN) return 0;

    uint32_t addr = FACTORY_IMG_BASE_ADDR + FACTORY_IMG_HEAD_LEN + img_offset;
    uint8_t *p = data_buf;                    /* 保存起点，CRC 还要用 */
    uint32_t left = len;

    while (left > 0) {
        uint32_t room = 256 - (addr & 0xFF);  /* 当前页剩余容量 */
        uint32_t n = (left < room) ? left : room;
        Int_W25Q64_PageProgram(addr, p, n);   /* 按页切，绝不跨页 */
        addr += n; p += n; left -= n;
    }

    img_offset += len;
    for (uint32_t i = 0; i < len; i++) {
        img_crc = Factory_CRC_Calculate(data_buf[i], img_crc);
    }
    return 1;
}

/**
 * @brief 对头部进行编写并提交
 * @param total_len
 * @return 0:编译失败   1:编译成功
 */
uint8_t Int_Factory_Commit(uint32_t total_len) {
    Factory_Image_Header hdr;
    // 1.判断程序大小是否合法
    if (total_len > FACTORY_IMG_MAX_LEN) return 0;
    if (total_len != img_offset) return 0;

    // 2.编写头部
    hdr.magic = FACTORY_IMG_MAGIC;
    hdr.length = total_len;
    hdr.crc32 = ~img_crc;
    hdr.reserved = 0;
    // printf("Commit CRC:%x\r\n",~img_crc);
    Int_W25Q64_PageProgram(FACTORY_IMG_BASE_ADDR,(uint8_t *)&hdr,sizeof(hdr));
    return 1;
}

/**
 * @brief 检查备份程序的健康性
 * @return 0:程序异常 1:程序正常
 */
uint8_t Int_Factory_Check_Image(void) {
    Factory_Image_Header hdr;       //存储读取的头部
    uint32_t crc = 0xFFFFFFFFUL;    //CRC初始值
    uint8_t read_buf[256]={0};      //接收读取的数据

    // 1.读取头部
    Int_W25Q64_ReadData(FACTORY_IMG_BASE_ADDR,(uint8_t *)&hdr,sizeof(hdr));
    if (hdr.magic != FACTORY_IMG_MAGIC) return 0;                           //镜像判断
    if (hdr.length >FACTORY_IMG_MAX_LEN || hdr.length == 0) return 0;       //长度判断

    // 2.重新计算CRC
    for (uint32_t offset = 0; offset < hdr.length; offset+=256) {
        uint32_t chunk = (hdr.length - offset)>256 ? 256 : (hdr.length - offset);
        Int_W25Q64_ReadData(FACTORY_IMG_BASE_ADDR + FACTORY_IMG_HEAD_LEN + offset,read_buf,chunk);
        for (uint32_t i=0;i<chunk;i++) {
            crc = Factory_CRC_Calculate(read_buf[i],crc);
        }
    }
    // printf("Checked CRC: %x\r\n", ~crc);
    return (~crc == hdr.crc32);
}

/**
 * @brief 转移备份程序至芯片FLASH
 * @return 0:转移失败 1:转移成功
 */
uint8_t Int_Factory_Restore(void) {

    Factory_Image_Header hdr;
    uint8_t read_buf[256]={0};

    if (!Int_Factory_Check_Image()) return 0;

    // 1.读取头部
    Int_W25Q64_ReadData(FACTORY_IMG_BASE_ADDR,(uint8_t *)&hdr,sizeof(hdr));

    // 2.擦除APP区的内容
    Int_Bootloader_Erase_Flash(APP_START_ADDR,48);

    // 3.从W24Q64读取备份写入 --> APP区
    HAL_FLASH_Unlock();
    for (uint32_t offset = 0; offset < hdr.length; offset+=256) {
        uint32_t chunk = (hdr.length - offset)>256 ? 256 : (hdr.length - offset);
        Int_W25Q64_ReadData(FACTORY_IMG_BASE_ADDR + FACTORY_IMG_HEAD_LEN + offset,read_buf,chunk);
        for (uint32_t i=0;i<chunk;i+=2) {
            uint16_t halfword = 0xFFFFUL;
            uint16_t n = (chunk-i >= 2) ? 2 : (chunk-i);
            memcpy(&halfword,read_buf+i,n);
            HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,APP_START_ADDR+offset+i,halfword);
        }
    }
    HAL_FLASH_Lock();
    return 1;
}

uint8_t Int_Factory_Backup_From_App(uint32_t len) {

    uint8_t read_buf[256]={0};

    Int_Factory_Init();
    for (uint32_t offset = 0; offset < len; offset+=256) {
        uint32_t chunk = (len-offset)>256 ? 256 : (len-offset);
        memcpy(read_buf,(uint8_t *)(APP_START_ADDR+offset),chunk);
        if (Int_Factory_Image_Append(read_buf,chunk) == 0) return 0;
    }
    return Int_Factory_Commit(len);
}

// /* 备份功能全链路测试：2KB 假程序 */
// void Test_Factory_Backup(void)
// {
//     uint8_t buf[240], rbuf[256];
//     uint32_t len = 2048;
//     uint8_t ok = 1;
//
//     Int_Factory_Init();                          /* 擦镜像区 12 扇区 */
//
//     /* 模拟串口分包 */
//     for (uint32_t off = 0; off < len; off += 240) {
//         uint32_t n = (len - off > 240) ? 240 : (len - off);
//         for (uint32_t i = 0; i < n; i++) buf[i] = (uint8_t)(off + i);
//         if (!Int_Factory_Image_Append(buf, n)) { ok = 0; break; }
//     }
//     if (!Int_Factory_Commit(len)) ok = 0;
//     printf("Commit: %s\r\n", ok ? "OK" : "FAIL");
//
//     /* 读回逐字节比对 */
//     for (uint32_t off = 0; off < len; off += 256) {
//         uint32_t n = (len - off > 256) ? 256 : (len - off);
//         Int_W25Q64_ReadData(FACTORY_IMG_BASE_ADDR + FACTORY_IMG_HEAD_LEN + off, rbuf, n);
//         for (uint32_t i = 0; i < n; i++)
//             if (rbuf[i] != (uint8_t)(off + i)) { ok = 0; break; }
//     }
//     printf("Data verify: %s\r\n", ok ? "OK" : "FAIL");
//
//     printf("Check: %d (expect 1)\r\n", Int_Factory_Check_Image());
//
//     /* 反证：0x00010F 原值 0xEF，写成 0xEE（清 bit0）——真正篡改 */
//     uint8_t z = 0xEE;
//     Int_W25Q64_PageProgram(FACTORY_IMG_BASE_ADDR + FACTORY_IMG_HEAD_LEN + 255, &z, 1);
//     printf("After tamper: %d (expect 0)\r\n", Int_Factory_Check_Image());
// }
//
// /* 恢复出厂测试：2KB 假镜像恢复回 APP 区并比对（会擦掉 App 区现有内容！） */
// void Test_Factory_Restore(void)
// {
//     uint8_t buf[240], rbuf[256];
//     uint32_t len = 2048;
//
//     /* 重新写入一份干净镜像（上一轮 Backup 结尾篡改过，镜像已坏） */
//     Int_Factory_Init();
//     for (uint32_t off = 0; off < len; off += 240) {
//         uint32_t n = (len - off > 240) ? 240 : (len - off);
//         for (uint32_t i = 0; i < n; i++) buf[i] = (uint8_t)(off + i);
//         Int_Factory_Image_Append(buf, n);
//     }
//     Int_Factory_Commit(len);
//
//     if (!Int_Factory_Check_Image()) {
//         printf("No image, run Test_Factory_Backup first\r\n");
//         return;
//     }
//
//     uint8_t ret = Int_Factory_Restore();              /* 擦 App 区 + 写回 */
//     printf("Restore: %d (expect 1)\r\n", ret);
//
//     /* 读回 APP 区逐字节比对（内部 Flash 可直接指针读） */
//     uint8_t ok = 1;
//     for (uint32_t off = 0; off < len; off += 256) {
//         uint32_t n = (len - off > 256) ? 256 : (len - off);
//         memcpy(rbuf, (uint8_t *)(APP_START_ADDR + off), n);
//         for (uint32_t i = 0; i < n; i++)
//             if (rbuf[i] != (uint8_t)(off + i)) { ok = 0; break; }
//     }
//     printf("APP verify: %s\r\n", ok ? "OK" : "FAIL");
// }
//
// /* 真实闭环：备份当前 App → 擦掉 → 恢复 → 跳转（App 区必须先烧着能跑的程序！） */
// void Test_Factory_Real(void)
// {
//     uint8_t buf[256];
//     uint32_t len = APP_END_ADDR - APP_START_ADDR;      /* 48KB 全量备份 */
//     uint8_t ok = 1;
//
//     /* 0. 检查 App 区是否有程序（向量表非全 FF） */
//     uint32_t *vt = (uint32_t *)APP_START_ADDR;
//     if (vt[0] == 0xFFFFFFFFUL || vt[1] == 0xFFFFFFFFUL) {
//         printf("App is empty, flash TEST first\r\n");
//         return;
//     }
//
//     /* 1. 备份：内部 Flash → W25Q64（出厂镜像） */
//     Int_Factory_Init();
//     for (uint32_t off = 0; off < len; off += 256) {
//         memcpy(buf, (uint8_t *)(APP_START_ADDR + off), 256);
//         if (!Int_Factory_Image_Append(buf, 256)) { ok = 0; break; }
//     }
//     if (!Int_Factory_Commit(len)) ok = 0;
//     printf("Backup: %s\r\n", ok ? "OK" : "FAIL");
//
//     /* 2. 校验镜像 */
//     printf("Check: %d (expect 1)\r\n", Int_Factory_Check_Image());
//
//     /* 3. 擦掉 App 区（模拟程序损坏） */
//     Int_Bootloader_Erase_Flash(APP_START_ADDR, 48);
//     printf("App erased\r\n");
//
//     /* 4. 恢复出厂镜像 */
//     printf("Restore: %d (expect 1)\r\n", Int_Factory_Restore());
//
//     /* 5. 跳转验证：LED 应恢复闪烁（打印到此为止，App 接管） */
//     Int_Bootloader_Jump_to_app();
// }
