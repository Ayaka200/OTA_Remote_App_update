/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "can.h"
#include "i2c.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_update.h"
#include "Int_can.h"
#include "int_w25q64.h"
#include "int_at24c02.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_START_ADDR 0x8005000
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
// CAN_TxHeaderTypeDef TxHeader1={
//   .StdId = 0x123,.ExtId = 0x00000000, .IDE = CAN_ID_STD, .RTR = CAN_RTR_DATA,
//   .DLC =5, .TransmitGlobalTime = DISABLE,
// };
//
// CAN_TxHeaderTypeDef TxHeader2={
//   .StdId = 0x000,.ExtId = 0x00000000, .IDE = CAN_ID_STD, .RTR = CAN_RTR_DATA,
//   .DLC =4, .TransmitGlobalTime = DISABLE,
// };
// uint8_t CMD[4] = {0xAA,0xBB,0xAA,0xBB};
//
// CAN_Msg_t RxMsg;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  // RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;   // 使能 GPIOC 时钟
  // GPIOC->CRH &= ~(0xF << 20);            // 清除 PC13 配置
  // GPIOC->CRH |= (0x3 << 20);             // PC13 推挽输出，50MHz
  // GPIOC->BRR = GPIO_PIN_13;              // 点亮（低电平）

  //重定向中断向量表
  __disable_irq();
  SCB->VTOR = APP_START_ADDR;
  __enable_irq();

  /* 翻转1表示VTOR完了 */
  // GPIOC->BSRR = GPIO_PIN_13;
  // for(volatile int i=0;i<500000;i++);
  // GPIOC->BRR = GPIO_PIN_13;
  // //
  //  while(1);  // 死循环，看灯亮不亮

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
   HAL_Init();

  /* USER CODE BEGIN Init */
  /* 翻转1表示VTOR完了 */
  // GPIOC->BSRR = GPIO_PIN_13;
  // for(volatile int i=0;i<500000;i++);
  // GPIOC->BRR = GPIO_PIN_13;
  /* USER CODE END Init */

  /* Configure the system clock */
   SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  // GPIOC->BSRR = GPIO_PIN_13;
  // while(1);
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_CAN_Init();
  MX_SPI1_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */


   App_Update_Init();

  // uint8_t mf_id;
  // uint16_t chip_id;
  // Int_W25Q64_Read_ID(&mf_id, &chip_id);
  // printf("JEDEC: mf=%02X id=%04X\r\n", mf_id, chip_id);
  // printf("APP started\r\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  // HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
  while (1)
  {
    App_Update_Work();
    // HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    // HAL_Delay(500);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  //
  //HAL_RCC_DeInit();  // ← 加在这里，把时钟复位到复位默认状态（HSI）
  //

  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
