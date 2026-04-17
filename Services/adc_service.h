#ifndef ADC_SERVICE_H
#define ADC_SERVICE_H

#include <stdint.h>

void     adc_service_init(void);
void     adc_service_process(void);

uint16_t adc_get_voltage_mv(uint8_t idx);
int16_t  adc_get_current_ma(uint8_t idx);
int16_t  adc_get_temp(uint8_t idx);
uint16_t adc_get_raw_avg(uint8_t idx);

/* Direct access to the DMA buffer (passed to HAL_ADC_Start_DMA) */
volatile uint16_t *adc_get_dma_buf(void);

/* Calibration offset for current channels (raw ADC units) */
void     adc_set_current_offset(uint8_t ch, uint16_t offset_raw);
uint16_t adc_get_current_offset(uint8_t ch);

#endif /* ADC_SERVICE_H */
