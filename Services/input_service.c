#include "input_service.h"
#include "config.h"
#include "main.h"

#define INPUT_COUNT  9  /* PGOOD, SUS_S3, FAULTZ, IN_0..IN_5 */

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} input_pin_t;

/* Pin map: [0]=PGOOD, [1]=SUS_S3, [2]=FAULTZ, [3..8]=IN_0..IN_5 */
static const input_pin_t pins[INPUT_COUNT] = {
    { PGOOD_GPIO_Port,  PGOOD_Pin  },
    { SUS_S3_GPIO_Port, SUS_S3_Pin },
    { FAULTZ_GPIO_Port, FAULTZ_Pin },
    { IN_0_GPIO_Port,   IN_0_Pin   },
    { IN_1_GPIO_Port,   IN_1_Pin   },
    { IN_2_GPIO_Port,   IN_2_Pin   },
    { IN_3_GPIO_Port,   IN_3_Pin   },
    { IN_4_GPIO_Port,   IN_4_Pin   },
    { IN_5_GPIO_Port,   IN_5_Pin   },
};

static uint8_t  filtered[INPUT_COUNT];
static uint8_t  raw_prev[INPUT_COUNT];
static uint32_t last_change[INPUT_COUNT];

void input_service_init(void)
{
    for (uint8_t i = 0; i < INPUT_COUNT; i++) {
        uint8_t val = (HAL_GPIO_ReadPin(pins[i].port, pins[i].pin) == GPIO_PIN_SET) ? 1u : 0u;
        filtered[i]    = val;
        raw_prev[i]    = val;
        last_change[i] = systick_ms;
    }
}

void input_service_process(void)
{
    uint32_t now = systick_ms;

    for (uint8_t i = 0; i < INPUT_COUNT; i++) {
        uint8_t raw = (HAL_GPIO_ReadPin(pins[i].port, pins[i].pin) == GPIO_PIN_SET) ? 1u : 0u;

        if (raw != raw_prev[i]) {
            raw_prev[i]    = raw;
            last_change[i] = now;
        } else if (raw != filtered[i]) {
            if ((now - last_change[i]) >= DEBOUNCE_MS)
                filtered[i] = raw;
        }
    }
}

uint8_t input_get_pgood(void)  { return filtered[0]; }
uint8_t input_get_sus_s3(void) { return filtered[1]; }
uint8_t input_get_faultz(void) { return filtered[2]; }

uint8_t input_get_in(uint8_t idx)
{
    if (idx < 6) return filtered[3 + idx];
    return 0;
}

uint8_t input_get_packed(void)
{
    uint8_t v = 0;
    for (uint8_t i = 0; i < 6; i++)
        v |= (uint8_t)(filtered[3 + i] << i);
    v |= (uint8_t)(filtered[0] << 6);  /* PGOOD */
    v |= (uint8_t)(filtered[2] << 7);  /* Faultz */
    return v;
}
