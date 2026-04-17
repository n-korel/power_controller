#ifndef STM32F0XX_HAL_STUB_H
#define STM32F0XX_HAL_STUB_H

#include <stdint.h>
#include <stddef.h>

/* ===== GPIO ===== */
typedef struct {
    uint32_t tag;
} GPIO_TypeDef;

typedef enum {
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET   = 1
} GPIO_PinState;

#define GPIO_PIN_0   ((uint16_t)0x0001)
#define GPIO_PIN_1   ((uint16_t)0x0002)
#define GPIO_PIN_2   ((uint16_t)0x0004)
#define GPIO_PIN_3   ((uint16_t)0x0008)
#define GPIO_PIN_4   ((uint16_t)0x0010)
#define GPIO_PIN_5   ((uint16_t)0x0020)
#define GPIO_PIN_6   ((uint16_t)0x0040)
#define GPIO_PIN_7   ((uint16_t)0x0080)
#define GPIO_PIN_8   ((uint16_t)0x0100)
#define GPIO_PIN_9   ((uint16_t)0x0200)
#define GPIO_PIN_10  ((uint16_t)0x0400)
#define GPIO_PIN_11  ((uint16_t)0x0800)
#define GPIO_PIN_12  ((uint16_t)0x1000)
#define GPIO_PIN_13  ((uint16_t)0x2000)
#define GPIO_PIN_14  ((uint16_t)0x4000)
#define GPIO_PIN_15  ((uint16_t)0x8000)

/* ===== HAL status ===== */
typedef enum {
    HAL_OK      = 0x00,
    HAL_ERROR   = 0x01,
    HAL_BUSY    = 0x02,
    HAL_TIMEOUT = 0x03
} HAL_StatusTypeDef;

/* ===== UART ===== */
typedef struct {
    uint32_t dummy;
} UART_HandleTypeDef;

/* ===== TIM ===== */
typedef struct {
    struct {
        uint32_t CCR1;
    } Instance_data;
} TIM_HandleTypeDef;

#define TIM_CHANNEL_1  0x00000000U

#define __HAL_TIM_SET_COMPARE(__HANDLE__, __CHANNEL__, __COMPARE__) \
    ((__HANDLE__)->Instance_data.CCR1 = (__COMPARE__))

/* ===== FLASH ===== */
#define FLASH_TYPEERASE_PAGES  0x00U
#define FLASH_TYPEPROGRAM_WORD 0x02U

typedef struct {
    uint32_t  TypeErase;
    uintptr_t PageAddress; /* widened for 64-bit host tests */
    uint32_t  NbPages;
} FLASH_EraseInitTypeDef;

/* ===== IWDG ===== */
typedef struct {
    uint32_t dummy;
} IWDG_HandleTypeDef;

/* ===== Spy: recording GPIO writes for test assertions ===== */
#define HAL_STUB_GPIO_LOG_SIZE 64

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    GPIO_PinState state;
} gpio_write_record_t;

extern gpio_write_record_t hal_gpio_log[HAL_STUB_GPIO_LOG_SIZE];
extern uint32_t hal_gpio_log_count;
void hal_stub_reset(void);

/* Configurable return values for ReadPin */
#define HAL_STUB_READ_PIN_MAP_SIZE 32
typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    GPIO_PinState value;
} gpio_read_map_entry_t;

extern gpio_read_map_entry_t hal_gpio_read_map[HAL_STUB_READ_PIN_MAP_SIZE];
extern uint32_t hal_gpio_read_map_count;
void hal_stub_set_pin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState val);

/* ===== HAL function declarations (stubs) ===== */
void           HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState);
GPIO_PinState  HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size);
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *htim, uint32_t Channel);
HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t TypeProgram, uintptr_t Address, uint64_t Data);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *pEraseInit, uint32_t *PageError);
HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *hiwdg);

/* Cortex-M stubs (instrumented for bootloader tests) */
void __set_MSP(uint32_t topOfMainStack);
void NVIC_SystemReset(void);
void __DSB(void);

extern uint32_t hal_stub_nvic_reset_count;
extern uint32_t hal_stub_set_msp_count;
extern uint32_t hal_stub_set_msp_last_value;

/* Error handler (from main.h) */
void Error_Handler(void);

#endif /* STM32F0XX_HAL_STUB_H */
