/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019
 *    William D. Jones (thor0505@comcast.net),
 *    Ha Thach (tinyusb.org)
 *    Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * This file is part of the TinyUSB stack.
 */

/* metadata:
   manufacturer: STMicroelectronics
*/

#include "stm32n6xx_hal.h"
#include "bsp/board_api.h"

TU_ATTR_UNUSED static void Error_Handler(void) { }

static void MPU_Config(void);
static void RISAF_Config(void);
void HardFault_Handler(void);

typedef struct {
  GPIO_TypeDef* port;
  GPIO_InitTypeDef pin_init;
  uint8_t active_state;
} board_pindef_t;

#include "board.h"

//--------------------------------------------------------------------+
// MACRO TYPEDEF CONSTANT ENUM
//--------------------------------------------------------------------+

#ifdef UART_DEV
UART_HandleTypeDef UartHandle = {
  .Instance = UART_DEV,
  .Init = {
    .BaudRate = CFG_BOARD_UART_BAUDRATE,
    .WordLength = UART_WORDLENGTH_8B,
    .StopBits = UART_STOPBITS_1,
    .Parity = UART_PARITY_NONE,
    .HwFlowCtl = UART_HWCONTROL_NONE,
    .Mode = UART_MODE_TX_RX,
    .OverSampling = UART_OVERSAMPLING_16,
  }
};
#endif

#ifndef SWO_FREQ
#define SWO_FREQ  4000000
#endif

//--------------------------------------------------------------------+
// Forward USB interrupt events to TinyUSB IRQ Handler
//--------------------------------------------------------------------+

// Despite being call USB2_OTG_FS on some MCUs
// OTG_FS is marked as RHPort0 by TinyUSB to be consistent across stm32 port
void USB2_OTG_HS_IRQHandler(void) {
  tusb_int_handler(0, true);
}

// Despite being call USB1_OTG_HS on some MCUs
// OTG_HS is marked as RHPort1 by TinyUSB to be consistent across stm32 port
void USB1_OTG_HS_IRQHandler(void) {
  tusb_int_handler(1, true);
}

#ifdef TRACE_ETM
// Not implemented in this port
void trace_etm_init(void) {
  return
}
#else
  #define trace_etm_init()
#endif

#ifdef LOGGER_SWO
void log_swo_init(void)
{
  //UNLOCK FUNNEL
  *(volatile uint32_t*)(0x5C004FB0) = 0xC5ACCE55; // SWTF_LAR
  *(volatile uint32_t*)(0x5C003FB0) = 0xC5ACCE55; // SWO_LAR

  //SWO current output divisor register
  //To change it, you can use the following rule
  // value = (CPU_Freq / 3 / SWO_Freq) - 1
  *(volatile uint32_t*)(0x5C003010) = ((SystemCoreClock / 3 / SWO_FREQ) - 1); // SWO_CODR

  //SWO selected pin protocol register
  *(volatile uint32_t*)(0x5C0030F0) = 0x00000002; // SWO_SPPR

  //Enable ITM input of SWO trace funnel
  *(volatile uint32_t*)(0x5C004000) |= 0x00000001; // SWFT_CTRL
}
#else
  #define log_swo_init()
#endif

void board_init(void) {

  /* Enable BusFault and SecureFault handlers (HardFault is default) */
  SCB->SHCSR |= (SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_SECUREFAULTENA_Msk);

  HAL_PWREx_EnableVddA();
  HAL_PWREx_EnableVddIO2();
  HAL_PWREx_EnableVddIO3();
  HAL_PWREx_EnableVddIO4();
  HAL_PWREx_EnableVddIO5();

  MPU_Config();

  HAL_Init();

  // Implemented in board.h
  SystemClock_Config();

  RISAF_Config();

  // Enable All GPIOs clocks
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPION_CLK_ENABLE();
  __HAL_RCC_GPIOO_CLK_ENABLE();
  __HAL_RCC_GPIOP_CLK_ENABLE();
  __HAL_RCC_GPIOQ_CLK_ENABLE();

  log_swo_init();
  trace_etm_init();

  for (uint8_t i = 0; i < TU_ARRAY_SIZE(board_pindef); i++) {
    HAL_GPIO_Init(board_pindef[i].port, &board_pindef[i].pin_init);
  }

  NVIC_SetPriority(UCPD1_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),5, 0));
  NVIC_EnableIRQ(UCPD1_IRQn);

#if CFG_TUSB_OS == OPT_OS_NONE
  // 1ms tick timer
  SysTick_Config(SystemCoreClock / 1000);

#elif CFG_TUSB_OS == OPT_OS_FREERTOS
  // Explicitly disable systick to prevent its ISR runs before scheduler start
  SysTick->CTRL &= ~1U;

  // If freeRTOS is used, IRQ priority is limit by max syscall ( smaller is higher )

  NVIC_SetPriority(OTG_HS_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY );
#endif



#ifdef UART_DEV
  UART_CLK_EN();
  HAL_UART_Init(&UartHandle);
#endif


  __HAL_RCC_USB1_OTG_HS_CLK_ENABLE();
  PCD_HandleTypeDef hpcd_USB1_OTG_HS;
  /* USER CODE BEGIN USB1_OTG_HS_Init 1 */
  memset(&hpcd_USB1_OTG_HS, 0x0, sizeof(PCD_HandleTypeDef));

  /* USER CODE END USB1_OTG_HS_Init 1 */
  hpcd_USB1_OTG_HS.Instance = USB1_OTG_HS;
  hpcd_USB1_OTG_HS.Init.dev_endpoints = 9;
  hpcd_USB1_OTG_HS.Init.speed = PCD_SPEED_HIGH;
  hpcd_USB1_OTG_HS.Init.dma_enable = DISABLE;
  hpcd_USB1_OTG_HS.Init.phy_itface = USB_OTG_HS_EMBEDDED_PHY;
  hpcd_USB1_OTG_HS.Init.Sof_enable = DISABLE;
  hpcd_USB1_OTG_HS.Init.low_power_enable = DISABLE;
  hpcd_USB1_OTG_HS.Init.lpm_enable = DISABLE;
  hpcd_USB1_OTG_HS.Init.vbus_sensing_enable = DISABLE;
  hpcd_USB1_OTG_HS.Init.use_dedicated_ep1 = DISABLE;
  hpcd_USB1_OTG_HS.Init.use_external_vbus = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB1_OTG_HS) != HAL_OK)
  {
    HardFault_Handler();
  }

  board_init2();

#if CFG_TUH_ENABLED
  board_vbus_set(BOARD_TUH_RHPORT, 1);
#endif
}

//--------------------------------------------------------------------+
// Board porting API
//--------------------------------------------------------------------+

void board_led_write(bool state) {
#ifdef PINID_LED
  board_pindef_t* pindef = &board_pindef[PINID_LED];
  GPIO_PinState pin_state = state == pindef->active_state ? GPIO_PIN_SET : GPIO_PIN_RESET;
  HAL_GPIO_WritePin(pindef->port, pindef->pin_init.Pin, pin_state);
#else
  (void) state;
#endif
}

uint32_t board_button_read(void) {
#ifdef PINID_BUTTON
  board_pindef_t* pindef = &board_pindef[PINID_BUTTON];
  return pindef->active_state == HAL_GPIO_ReadPin(pindef->port, pindef->pin_init.Pin);
#else
  return 0;
#endif
}

size_t board_get_unique_id(uint8_t id[], size_t max_len) {
  (void) max_len;
  volatile uint32_t * stm32_uuid = (volatile uint32_t *) UID_BASE;
  uint32_t* id32 = (uint32_t*) (uintptr_t) id;
  uint8_t const len = 12;

  id32[0] = stm32_uuid[0];
  id32[1] = stm32_uuid[1];
  id32[2] = stm32_uuid[2];

  return len;
}

int board_uart_read(uint8_t *buf, int len) {
  (void) buf;
  (void) len;
  return 0;
}

int board_uart_write(void const *buf, int len) {
#ifdef UART_DEV
  HAL_UART_Transmit(&UartHandle, (uint8_t * )(uintptr_t)
  buf, len, 0xffff);
  return len;
#else
  (void) buf; (void) len;
  return -1;
#endif
}

#if CFG_TUSB_OS == OPT_OS_NONE
volatile uint32_t system_ticks = 0;

void SysTick_Handler(void) {
  HAL_IncTick();
  system_ticks++;
}

uint32_t board_millis(void) {
  return system_ticks;
}

#endif

/* MPU Configuration */
static void MPU_Config(void)
{
  MPU_Region_InitTypeDef default_config = {0};
  MPU_Attributes_InitTypeDef attr_config = {0};
  uint32_t primask_bit = __get_PRIMASK();
  __disable_irq();

  /* disable the MPU */
  HAL_MPU_Disable();

  /* create an attribute configuration for the MPU */
  attr_config.Attributes = INNER_OUTER(MPU_NOT_CACHEABLE);
  attr_config.Number = MPU_ATTRIBUTES_NUMBER0;

  HAL_MPU_ConfigMemoryAttributes(&attr_config);

  /*Normal memory type, code execution allowed */
  default_config.Enable = MPU_REGION_ENABLE;
  default_config.Number = MPU_REGION_NUMBER0;
  default_config.BaseAddress = 0x341FA600;
  default_config.LimitAddress =  0x341FFFFF;
  default_config.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  default_config.AccessPermission = MPU_REGION_ALL_RW;
  default_config.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  default_config.AttributesIndex = MPU_ATTRIBUTES_NUMBER0;
  HAL_MPU_ConfigRegion(&default_config);

  /* enable the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

  /* Exit critical section to lock the system and avoid any issue around MPU mechanisme */
  __set_PRIMASK(primask_bit);
}


/**
* @brief  RISAF Configuration.
* @retval None
*/
static void RISAF_Config(void)
{
  RIMC_MasterConfig_t RIMC_master = {0};

  __HAL_RCC_RIFSC_CLK_ENABLE();

  RIMC_master.MasterCID = RIF_CID_1;
  RIMC_master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;

  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_OTG1, &RIMC_master);
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_OTG2, &RIMC_master);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_OTG1HS, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_OTG2HS, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);

  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_ADC12, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);

}


/**
* @brief PCD MSP Initialization
* This function configures the hardware resources used in this example
* @param hhcd: PCD handle pointer
* @retval None
*/
void HAL_PCD_MspInit(PCD_HandleTypeDef* pcdHandle)
{
  if (pcdHandle->Instance==USB1_OTG_HS)
  {
    /* USER CODE BEGIN USB_OTG_HS_MspInit 0 */

    /* USER CODE END USB_OTG_HS_MspInit 0 */
    /* Enable VDDUSB */
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWREx_EnableVddUSBVMEN();
    while(__HAL_PWR_GET_FLAG(PWR_FLAG_USB33RDY));
    HAL_PWREx_EnableVddUSB();

    /** Initializes the peripherals clock
    */
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USBOTGHS1;
    PeriphClkInitStruct.UsbOtgHs1ClockSelection = RCC_USBPHY1REFCLKSOURCE_HSE_DIRECT;

    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      /* Initialization Error */
      Error_Handler();
    }

    /** Set USB OTG HS PHY1 Reference Clock Source */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USBPHY1;
    PeriphClkInitStruct.UsbPhy1ClockSelection = RCC_USBPHY1REFCLKSOURCE_HSE_DIRECT;

    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      /* Initialization Error */
      Error_Handler();
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();

    LL_AHB5_GRP1_ForceReset(0x00800000);
    __HAL_RCC_USB1_OTG_HS_FORCE_RESET();
    __HAL_RCC_USB1_OTG_HS_PHY_FORCE_RESET();

    LL_RCC_HSE_SelectHSEDiv2AsDiv2Clock();
    LL_AHB5_GRP1_ReleaseReset(0x00800000);

    /* Peripheral clock enable */
    __HAL_RCC_USB1_OTG_HS_CLK_ENABLE();

    /* Required few clock cycles before accessing USB PHY Controller Registers */
    HAL_Delay(1);

    USB1_HS_PHYC->USBPHYC_CR &= ~(0x7 << 0x4);

    USB1_HS_PHYC->USBPHYC_CR |= (0x1 << 16) |
                                (0x2 << 4)  |
                                (0x1 << 2)  |
                                 0x1U;

    __HAL_RCC_USB1_OTG_HS_PHY_RELEASE_RESET();

    /* Required few clock cycles before Releasing Reset */
    HAL_Delay(1);

    __HAL_RCC_USB1_OTG_HS_RELEASE_RESET();

    /* Peripheral PHY clock enable */
    __HAL_RCC_USB1_OTG_HS_PHY_CLK_ENABLE();

    /* USB_OTG_HS interrupt Init */
    HAL_NVIC_SetPriority(USB1_OTG_HS_IRQn, 7, 0);
    HAL_NVIC_EnableIRQ(USB1_OTG_HS_IRQn);

    /* USER CODE BEGIN USB_OTG_HS_MspInit 1 */

    /* USER CODE END USB_OTG_HS_MspInit 1 */
  }
}

void HardFault_Handler(void) {
  __asm("BKPT #0\n");
}

// Required by __libc_init_array in startup code if we are compiling using
// -nostdlib/-nostartfiles.
void _init(void) {
}
