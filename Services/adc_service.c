#include "adc_service.h"
#include "config.h"

/* DMA target buffer — passed to HAL_ADC_Start_DMA */
static volatile uint16_t dma_buf[ADC_CHANNEL_COUNT];

/* Sliding window */
static uint16_t window[ADC_CHANNEL_COUNT][ADC_WINDOW_SIZE];
static uint8_t  window_idx;
static uint8_t  window_fill;

/* Averaged raw values */
static uint16_t avg_raw[ADC_CHANNEL_COUNT];

/* Physical values */
static uint16_t voltage_mv[4];   /* v24, v12, v5, v3v3 */
static int16_t  current_ma[5];   /* lcd, bl, scaler, audio_l, audio_r */

/* Calibration offsets (raw ADC units, default = 1650mV in raw) */
static uint16_t current_offset_raw[CURRENT_CHANNELS];

/* ------------------------------------------------------------------ */
static uint16_t default_offset_raw(void)
{
    return (uint16_t)((uint32_t)CURRENT_VOFFSET_MV_DEFAULT * ADC_RESOLUTION / ADC_VREF_MV);
}

void adc_service_init(void)
{
    uint16_t def = default_offset_raw();
    for (uint8_t i = 0; i < CURRENT_CHANNELS; i++)
        current_offset_raw[i] = def;

    window_idx  = 0;
    window_fill = 0;

    for (uint8_t ch = 0; ch < ADC_CHANNEL_COUNT; ch++)
        for (uint8_t s = 0; s < ADC_WINDOW_SIZE; s++)
            window[ch][s] = 0;
}

void adc_service_process(void)
{
    /* Snapshot DMA buffer into window */
    for (uint8_t ch = 0; ch < ADC_CHANNEL_COUNT; ch++)
        window[ch][window_idx] = dma_buf[ch];

    window_idx++;
    if (window_idx >= ADC_WINDOW_SIZE)
        window_idx = 0;
    if (window_fill < ADC_WINDOW_SIZE)
        window_fill++;

    /* Compute averages */
    for (uint8_t ch = 0; ch < ADC_CHANNEL_COUNT; ch++) {
        uint32_t sum = 0;
        for (uint8_t s = 0; s < window_fill; s++)
            sum += window[ch][s];
        avg_raw[ch] = (uint16_t)(sum / window_fill);
    }

    /* Convert voltages via shared ADC_RAIL_MV_FROM_RAW (config.h):
     * adc_mv = raw * 2500 / 4096, then vin = adc_mv * 11616 / 1000 */
    static const uint8_t v_idx[4] = {
        ADC_IDX_V24, ADC_IDX_V12, ADC_IDX_V5, ADC_IDX_V3V3
    };
    for (uint8_t i = 0; i < 4; i++) {
        voltage_mv[i] = (uint16_t)ADC_RAIL_MV_FROM_RAW(avg_raw[v_idx[i]]);
    }

    /* Convert currents: i_ma = (adc_mv - offset_mv) * 1000 / 264 */
    static const uint8_t c_idx[5] = {
        ADC_IDX_LCD_CURRENT, ADC_IDX_BL_CURRENT, ADC_IDX_SCALER_CURRENT,
        ADC_IDX_AUDIO_L_CURRENT, ADC_IDX_AUDIO_R_CURRENT
    };
    for (uint8_t i = 0; i < CURRENT_CHANNELS; i++) {
        uint32_t adc_mv    = (uint32_t)avg_raw[c_idx[i]] * ADC_VREF_MV / ADC_RESOLUTION;
        uint32_t offset_mv = (uint32_t)current_offset_raw[i] * ADC_VREF_MV / ADC_RESOLUTION;
        int32_t  diff_mv   = (int32_t)adc_mv - (int32_t)offset_mv;
        current_ma[i] = (int16_t)(diff_mv * 1000 / (int32_t)CURRENT_SENSITIVITY_MV_PER_A);
    }
}

/* ---- Public getters ---- */

volatile uint16_t *adc_get_dma_buf(void)
{
    return dma_buf;
}

uint16_t adc_get_voltage_mv(uint8_t idx)
{
    if (idx < 4) return voltage_mv[idx];
    return 0;
}

int16_t adc_get_current_ma(uint8_t idx)
{
    if (idx < CURRENT_CHANNELS) return current_ma[idx];
    return 0;
}

int16_t adc_get_temp(uint8_t idx)
{
    (void)idx;
    return -32768;  /* NTC not installed (Rules 2.4) */
}

uint16_t adc_get_raw_avg(uint8_t idx)
{
    if (idx < ADC_CHANNEL_COUNT) return avg_raw[idx];
    return 0;
}

void adc_set_current_offset(uint8_t ch, uint16_t offset_raw)
{
    if (ch < CURRENT_CHANNELS)
        current_offset_raw[ch] = offset_raw;
}

uint16_t adc_get_current_offset(uint8_t ch)
{
    if (ch < CURRENT_CHANNELS) return current_offset_raw[ch];
    return default_offset_raw();
}
