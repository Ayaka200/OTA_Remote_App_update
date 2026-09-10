//
// Created by 23029 on 2026/9/10.
//

#include "App_update.h"

APP_UPDATE_STATUS app_update_status = APP_UPDATE_WAIT;
/**
 * @brief 对上位机的程序进行初始化
 */
void App_Update_Init(void) {

    printf("app_update_init\r\n");
    Int_CAN_Init();
    app_update_status = APP_UPDATE_WAIT;
}

/**
 * @brief 等待发送指令
 */
void App_Update_WaitCmd(void) {

}
/**
 * @brief 发送程序
 */
void App_Update_SendApp(void) {

}
/**
 * @brief 上位机程序上传APP执行
 */
void App_Update_work(void) {
    switch (app_update_status) {
        case APP_UPDATE_WAIT:
            break;

        case APP_UPDATE_SEND_APP:
            break;

        default:
            break;
    }
}