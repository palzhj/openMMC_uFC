/**
 ******************************************************************************
 * @file    main.c
 * @author  Jie Zhang
 * @version V1.0
 * @date    11-February-2025
 * @brief   Main program body
 ******************************************************************************
 *
 * This file provides a basic structure and includes functions for initializing
 * peripherals, handling firmware updates, and managing the bootloader process.
 *
 * The application begins by initializing the UART for debugging purposes and
 * checking for firmware updates. If a valid firmware update is detected, it
 * proceeds to update the application or bootloader firmware. After completing
 * the update, the code jumps to the user application to continue execution.
 *
 * The code also includes helper functions for converting integers to strings,
 * sending strings over UART, and handling errors during the firmware update
 * process. Additionally, it provides a mechanism for retargeting the C library's
 * printf function to the UART for debugging output.
 *
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "stm32f10x_conf.h"
#include <stdio.h>
#include <string.h>

/* Private typedef -----------------------------------------------------------*/
enum fw_update_type
{
  FW_UPDATE_APP = 1,
  FW_UPDATE_BOOT = 2,
};

typedef struct
{
  uint8_t version[3];
  uint8_t fw_type;
  uint32_t magic;
} fw_info;

/* Private define ------------------------------------------------------------*/
/* Define the STM32F10x FLASH Page Size depending on the used STM32 device */
#if defined(STM32F10X_HD) || defined(STM32F10X_HD_VL) || defined(STM32F10X_CL) || defined(STM32F10X_XL)
#define FLASH_PAGE_SIZE ((uint16_t)0x800)
#else
#define FLASH_PAGE_SIZE ((uint16_t)0x400)
#endif

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
extern const uint32_t __BootFlash_start;
extern const uint32_t __BootFlash_end;
extern const uint32_t __AppFlash_start;
extern const uint32_t __AppFlash_end;
extern const uint32_t __FWUpdateFlash_start;
extern const uint32_t __FWUpdateFlash_end;
extern const fw_info __FWInfo_addr;

const fw_info *fw_header = &__FWInfo_addr;

/* Private function prototypes -----------------------------------------------*/
#ifdef __GNUC__
/* With GCC/RAISONANCE, small printf (option LD Linker->Libraries->Small printf
  set to 'Yes') calls __io_putchar() */
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif /* __GNUC__ */

/* Function pointer for jumping to user application. */
typedef void (*app_ptr)(void);

/* Private functions ---------------------------------------------------------*/

/**
 * @brief  Converts an 8-bit unsigned integer to a string.
 * @param  n: The 8-bit unsigned integer to be converted.
 * @param  str: Pointer to the buffer where the resulting string will be stored.
 * @retval Pointer to the resulting null-terminated string.
 *
 * This function takes an 8-bit unsigned integer and converts it into a string
 * representation. The resulting string is stored in the buffer pointed to by
 * the 'str' parameter. The function handles integers from 0 to 255 and ensures
 * that the resulting string is null-terminated. The buffer is expected to be
 * large enough to hold up to four characters (including the null terminator).
 */
char *u8_to_str(uint8_t n, char *str)
{
  uint8_t digits[3];
  uint8_t index = 0;

  // Extract hundreds, tens, and units place
  digits[0] = n / 100;
  digits[1] = (n / 10) % 10;
  digits[2] = n % 10;

  // Skip leading zeros
  if (digits[0] != 0)
  {
    str[index++] = digits[0] + '0';
  }
  if (digits[0] != 0 || digits[1] != 0)
  {
    str[index++] = digits[1] + '0';
  }

  // Add the last digit
  str[index++] = digits[2] + '0';

  // Null terminate the string
  str[index] = '\0';

  return str;
}

/**
 * @brief  Sends a string over USART1 in a blocking manner.
 * @param  buffer: Pointer to the null-terminated string to be sent.
 * @retval The number of characters sent.
 *
 * This function transmits a string via USART1, waiting for each character
 * to be successfully transmitted before sending the next one. It uses the
 * USART_GetFlagStatus() function to check the Transfer Complete (TC) flag
 * and USART_SendData() to send each character. The function returns the
 * total number of characters sent.
 */
int uart1_write_str_blocking(const char *buffer)
{
  int len = 0;
  while (*buffer != '\0')
  {
    /* Wait for the transmission to complete by checking the Transfer Complete (TC) flag */
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET)
      ;
    USART_SendData(USART1, (uint16_t)*buffer++);
    len++;
  }
  return len;
}

/**
 * @brief  Jumps to the user application code.
 * @param  None
 * @retval None
 *
 * This function is responsible for transitioning the execution flow from
 * the bootloader to the user application. It performs necessary checks
 * and setups before jumping to the application code. The function first
 * verifies that the application start address is valid. It then sets the
 * Main Stack Pointer (MSP) to the application's start address and jumps
 * to the application's reset handler to begin execution. Interrupts are
 * disabled during this process to ensure a smooth transition.
 */
void bootloader_jump_to_app(void)
{
  /* __AppFlash_start is a single linker symbol (uint32_t). Indexing it as
   * app_start_addr[1] is undefined behaviour, and GCC optimised the jump to
   * load app_start_addr[0] (the SP) instead of the reset handler, jumping into
   * SRAM. Read the vector table with volatile loads instead. */
  uint32_t app_addr = (uint32_t)&__AppFlash_start;
  volatile uint32_t *vec = (volatile uint32_t *)app_addr;
  uint32_t app_sp = vec[0];
  uint32_t app_pc = vec[1];
  typedef void (*pfun)(void);
  static pfun jumpToApp;
  char hexbuf[3];

  /* Diagnostic: print the jump target repeatedly so a host capture can see it */
  for (int rep = 0; rep < 5; rep++)
  {
    uart1_write_str_blocking("[BOOTLOADER] jump app=");
    for (int i = 3; i >= 0; i--) uart1_write_str_blocking(u8_to_str((uint8_t)(app_addr >> (8*i)), hexbuf));
    uart1_write_str_blocking(" SP=");
    for (int i = 3; i >= 0; i--) uart1_write_str_blocking(u8_to_str((uint8_t)(app_sp >> (8*i)), hexbuf));
    uart1_write_str_blocking(" PC=");
    for (int i = 3; i >= 0; i--) uart1_write_str_blocking(u8_to_str((uint8_t)(app_pc >> (8*i)), hexbuf));
    uart1_write_str_blocking("\r\n");
    for (volatile uint32_t d = 0; d < 200000; d++);
  }

  // Disable interrupts to ensure a smooth transition
  __set_PRIMASK(1);

  if ((app_sp & 0x2FFE0000) == 0x20000000)
  {
    jumpToApp = (pfun)app_pc;
    __set_PSP(app_sp);
    __set_MSP(app_sp);
    jumpToApp();
  }
  else
  {
    uart1_write_str_blocking("[BOOTLOADER] invalid app vector\r\n");
  }
}

/**
 * @brief  Performs a firmware update by erasing and writing to the flash memory.
 * @param  ftype: The type of firmware update to perform, either application or bootloader.
 * @retval None
 *
 * This function handles the firmware update process by erasing the necessary
 * sections of flash memory and writing new firmware data to it. It first
 * identifies the start and stop addresses of the firmware section to be
 * updated based on the update type (application or bootloader). It then
 * unlocks the flash memory, clears any pending flags, and proceeds to erase
 * the flash pages one by one. After erasing, it writes the new firmware
 * data to the flash memory and verifies the written data by reading it back.
 * If any errors occur during the erase, write, or verification steps,
 * error messages are sent over UART. Finally, it erases the firmware update
 * section and locks the flash memory before jumping to the application code.
 */
void update(enum fw_update_type ftype)
{
    const uint32_t sourceAddr = (uint32_t)&__FWUpdateFlash_start;
    uint32_t counter;
    uint32_t nbrOfPage;
    uint32_t startAddr;
    uint32_t stopAddr;

    if (ftype == FW_UPDATE_APP)
    {
        startAddr = (uint32_t)&__AppFlash_start;
        stopAddr = (uint32_t)&__AppFlash_end;
        nbrOfPage = (stopAddr - startAddr + 1) / FLASH_PAGE_SIZE;
    }
    else if (ftype == FW_UPDATE_BOOT)
    {
        startAddr = (uint32_t)&__BootFlash_start;
        stopAddr = (uint32_t)&__BootFlash_end;
        nbrOfPage = (stopAddr - startAddr + 1) / FLASH_PAGE_SIZE;
    }
    else
    {
        return; // Invalid firmware update type, exit function
    }

    // Unlock the flash to enable the flash control register access
    FLASH_Unlock();

    // Clear all pending flags
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    // Erase flash firmware app or bootloader section
    for (counter = 0; counter < nbrOfPage; counter++)
    {
        if (FLASH_ErasePage(startAddr + (FLASH_PAGE_SIZE * counter)) != FLASH_COMPLETE)
        {
            // Error occurred while page erase
            uart1_write_str_blocking("[BOOTLOADER] Page erase ERROR");
            return;
        }
    }

    // Write flash firmware app or bootloader section
    for (counter = 0; counter < (stopAddr - startAddr); counter += 4)
    {
        if (FLASH_ProgramWord(startAddr + counter, *(volatile uint32_t *)(sourceAddr + counter)) != FLASH_COMPLETE)
        {
            // Error occurred while page write
            uart1_write_str_blocking("[BOOTLOADER] Page write ERROR");
            return;
        }
    }

    // Read back the content of the memory. If it is wrong, then report an error.
    for (counter = 0; counter < (stopAddr - startAddr); counter += 4)
    {
        if (*(volatile uint32_t *)(startAddr + counter) != *(volatile uint32_t *)(sourceAddr + counter))
        {
            // Error occurred while read back check
            uart1_write_str_blocking("[BOOTLOADER] Page read back and check ERROR");
            return;
        }
    }

    // Erase the whole firmware update (staging) section. This also clears the
    // page that holds the fw_info record, so a completed update is not applied
    // again on the next reset (the magic word must not survive the update).
    {
        const uint32_t stagingPages = ((uint32_t)&__FWUpdateFlash_end - (uint32_t)&__FWUpdateFlash_start + 1) / FLASH_PAGE_SIZE;
        for (counter = 0; counter < stagingPages; counter++)
        {
            if (FLASH_ErasePage((uint32_t)&__FWUpdateFlash_start + (FLASH_PAGE_SIZE * counter)) != FLASH_COMPLETE)
            {
                // Error occurred while page erase
                uart1_write_str_blocking("[BOOTLOADER] Page erase ERROR for update section");
                return;
            }
        }
    }

    // Erase the fw_info page (it now lives in the former 8 KB gap) so a
    // completed update is not applied again on the next reset.
    if (FLASH_ErasePage(((uint32_t)&__FWInfo_addr) & ~(FLASH_PAGE_SIZE - 1u)) != FLASH_COMPLETE)
    {
        uart1_write_str_blocking("[BOOTLOADER] Page erase ERROR for fw_info section");
        return;
    }

    // Lock the Flash to disable the flash control register access
    // Recommended to protect the FLASH MEMORY against possible unwanted operation
    FLASH_Lock();

    // Jump to application code
    bootloader_jump_to_app();
}

/**
 * @brief  Initializes the UART interface for debugging.
 * @param  None
 * @retval None
 *
 * This function configures the UART interface used for debugging purposes.
 * It sets up the necessary GPIO pins for UART transmission and reception
 * and configures the UART parameters such as baud rate, word length, stop bits,
 * parity, and hardware flow control. After configuration, the UART interface
 * is enabled to allow for communication.
 */
void DEBUG_UART_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStructure;
  USART_InitTypeDef USART_InitStructure;

  /* Enable GPIO and USART clocks */
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO | RCC_APB2Periph_USART1, ENABLE);

  /* Configure USART Tx as alternate function push-pull */
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOA, &GPIO_InitStructure);

  /* Configure USART Rx as input floating */
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
  GPIO_Init(GPIOA, &GPIO_InitStructure);

  /* Configure USART1 settings */
  USART_InitStructure.USART_BaudRate = 115200;
  USART_InitStructure.USART_WordLength = USART_WordLength_8b;
  USART_InitStructure.USART_StopBits = USART_StopBits_1;
  USART_InitStructure.USART_Parity = USART_Parity_No;
  USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
  USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;

  /* Initialize USART1 */
  USART_Init(USART1, &USART_InitStructure);

  /* Enable USART1 */
  USART_Cmd(USART1, ENABLE);
}

// void DEBUG_I2C_Init(void)
// {
//   GPIO_InitTypeDef GPIO_InitStruct;
//   I2C_InitTypeDef I2C_InitStructure;

//   /*!< I2C_SCL_GPIO_CLK and I2C_SDA_GPIO_CLK Periph clock enable */
//   RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
//   /*!< GPIO configuration */
//   /*!< Configure I2C pins: SCL(PB10) and SDA(PB11) */
//   GPIO_InitStruct.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
//   GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_OD;
//   GPIO_InitStruct.GPIO_Speed  = GPIO_Speed_2MHz;
//   GPIO_Init(GPIOB, &GPIO_InitStruct);

//   /*!< I2C Periph clock enable */
//   RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);  // Enable I2C2 clock

//   I2C_StructInit(&I2C_InitStructure);  // Reset I2C_InitStructure to default values

//   I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;               // Set to I2C mode
//   I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;       // Standard duty cycle (50%)
//   I2C_InitStructure.I2C_OwnAddress1 = 0x00;                 // No address for master mode
//   I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;               // Enable acknowledgment
//   I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;  // 7-bit addressing mode
//   I2C_InitStructure.I2C_ClockSpeed = 100000;                 // Set I2C clock speed to 100kHz (can be adjusted)

//   I2C_Init(I2C2, &I2C_InitStructure);  // Initialize the I2C peripheral

//   // Enable the I2C peripheral in master mode
//   I2C_Cmd(I2C2, ENABLE);  // Enable I2C peripheral

// }

// #define I2C_TIMEOUT 1000

// int I2CMasterWrite(uint8_t slave_addr, uint8_t *tx_buff, uint8_t buff_len)
// {
//   uint32_t timeout = I2C_TIMEOUT; // Set timeout duration
//   uint8_t i;
//   slave_addr = slave_addr <<1;

//   // Wait until the I2C peripheral is not busy
//   while (I2C_GetFlagStatus(I2C2, I2C_FLAG_BUSY))
//   {
//     if (timeout-- == 0)
//       return 1; // Timeout occurred while waiting for the I2C to be idle.
//   }

//   // Generate the START condition
//   I2C_GenerateSTART(I2C2, ENABLE);

//   // Wait for the START condition to be transmitted
//   timeout = I2C_TIMEOUT;
//   while (!I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_MODE_SELECT))
//   {
//     if (timeout-- == 0)
//       return 2; // Timeout occurred while waiting for the START condition to be transmitted.
//   }

//   // Send the slave address with the write mode (LSB = 0 for write)
//   I2C_Send7bitAddress(I2C2, slave_addr, I2C_Direction_Transmitter);

//   // Wait for the slave to acknowledge the address
//   timeout = I2C_TIMEOUT;
//   while (!I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
//   {
//     if (timeout-- == 0)
//       return 3; // Timeout occurred while waiting for the slave address acknowledgment.
//   }

//   // Send each byte of the data buffer
//   for (i = 0; i < buff_len; i++)
//   {
//     I2C_SendData(I2C2, tx_buff[i]);

//     // Wait for the byte to be transmitted
//     timeout = I2C_TIMEOUT;
//     while (!I2C_CheckEvent(I2C2, I2C_EVENT_MASTER_BYTE_TRANSMITTED))
//     {
//       if (timeout-- == 0)
//         return 4; // Timeout occurred while waiting for data byte transmission.
//     }
//   }

//   // Generate the STOP condition
//   I2C_GenerateSTOP(I2C2, ENABLE);

//   // Return success
//   return 0;
// }

/**
 * @brief  Main program.
 * @param  None
 * @retval None
 */
int main(void)
{
    /* At this stage the microcontroller clock setting is already configured,
     * this is done through SystemInit() function which is called from startup
     * file (startup_stm32f10x_xx.s) before branching to application main.
     * To reconfigure the default setting of SystemInit() function, refer to
     * system_stm32f10x.c file
     */

    /* Initialize UART for debugging */
    DEBUG_UART_Init();

    /* Print bootloader compilation information */
    uart1_write_str_blocking("[BOOTLOADER] Compiled: ");
    uart1_write_str_blocking(__DATE__);
    uart1_write_str_blocking(" ");
    uart1_write_str_blocking(__TIME__);
    uart1_write_str_blocking("\r\n");

    /* MCU identity: flash size (KB) @0x1FFFF7E0, DBGMCU_IDCODE @0xE0042000 */
    {
      uint16_t flash_kb = *(volatile uint16_t *)0x1FFFF7E0;
      uint32_t idcode = *(volatile uint32_t *)0xE0042000;
      char b[4];
      uart1_write_str_blocking("[BOOTLOADER] FLASH_KB=");
      uart1_write_str_blocking(u8_to_str((uint8_t)flash_kb, b));
      uart1_write_str_blocking(" DEV_ID=");
      uart1_write_str_blocking(u8_to_str((uint8_t)((idcode >> 8) & 0xFF), b));
      uart1_write_str_blocking(u8_to_str((uint8_t)(idcode & 0xFF), b));
      uart1_write_str_blocking(" REV_ID=");
      uart1_write_str_blocking(u8_to_str((uint8_t)((idcode >> 16) & 0xFF), b));
      uart1_write_str_blocking(u8_to_str((uint8_t)((idcode >> 24) & 0xFF), b));
      uart1_write_str_blocking("\r\n");
    }

    // #define ADDR_TCA9548A 0x70
    // DEBUG_I2C_Init();
    // uint8_t I2c_pmbus_en    = 1;  // bit7
    // uint8_t I2c_ddr_en      = 0;  // bit6
    // uint8_t I2c_rtm_en      = 0;  // bit5
    // uint8_t I2c_fpga_en     = 0;  // bit4
    // uint8_t I2c_firefly_en  = 0;  // bit3
    // uint8_t I2c_clk_en      = 0;  // bit2
    // uint8_t I2c_fmc1_en     = 0;  // bit1
    // uint8_t I2c_fmc0_en     = 0;  // bit0
    // uint8_t i2c_sw = (I2c_pmbus_en << 7) | (I2c_ddr_en << 6) | (I2c_rtm_en << 5) | (I2c_fpga_en << 4) |
    //     (I2c_firefly_en << 3) |  (I2c_clk_en << 2) |  (I2c_fmc1_en << 1) |  I2c_fmc0_en;

    // while(I2CMasterWrite((uint16_t)ADDR_TCA9548A, &i2c_sw, 1)!=0)
    // {
    //   uart1_write_str_blocking("I2C MUX (TCA9548A) config ERROR!\r\n");
    // }

    /* Check for firmware update */
    if (fw_header->magic == 0xAAAAAAAA)
    {
        char version_str[4];  /* Buffer for version string conversion */

        uart1_write_str_blocking("[BOOTLOADER] DO NOT TURN OFF WHILE UPDATING!\r\n");

        /* Handle application firmware update */
        if (fw_header->fw_type == FW_UPDATE_APP)
        {
            uart1_write_str_blocking("[BOOTLOADER] New app firmware update found!\r\nUpdating to ");
            uart1_write_str_blocking(u8_to_str(fw_header->version[0], version_str));
            uart1_write_str_blocking(".");
            uart1_write_str_blocking(u8_to_str(fw_header->version[1], version_str));
            uart1_write_str_blocking(".");
            uart1_write_str_blocking(u8_to_str(fw_header->version[2], version_str));
            uart1_write_str_blocking("...\r\n");

            update(FW_UPDATE_APP);
        }
        /* Handle bootloader firmware update */
        else if (fw_header->fw_type == FW_UPDATE_BOOT)
        {
            uart1_write_str_blocking("[BOOTLOADER] New bootloader firmware update found!\r\nUpdating to ");
            uart1_write_str_blocking(u8_to_str(fw_header->version[0], version_str));
            uart1_write_str_blocking(".");
            uart1_write_str_blocking(u8_to_str(fw_header->version[1], version_str));
            uart1_write_str_blocking(".");
            uart1_write_str_blocking(u8_to_str(fw_header->version[2], version_str));
            uart1_write_str_blocking("...\r\n");

            update(FW_UPDATE_BOOT);
        }
        /* Handle unknown firmware type */
        else
        {
            uart1_write_str_blocking("[BOOTLOADER] ERROR: Unknown fw_type ");
            uart1_write_str_blocking(u8_to_str(fw_header->fw_type, version_str));
            uart1_write_str_blocking(" !\r\n Jumping to application code...\r\n");
        }
    }

    /* Jump to application code */
    bootloader_jump_to_app();

    /* Infinite loop - should never reach here */
    while (1)
    {
    }
}

/**
 * @brief  Retargets the C library printf function to the USART.
 * @param  None
 * @retval None
 */
PUTCHAR_PROTOTYPE
{
  /* Place your implementation of fputc here */
  /* Loop until the end of transmission */
  while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET)
    ;
  /* e.g. write a character to the USART */
  USART_SendData(USART1, (uint8_t)ch);
  return ch;
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
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

  /* Infinite loop */
  while (1)
  {
  }
}
#endif

/**
 * @}
 */
