#ifndef INPUT_SERVICE_H
#define INPUT_SERVICE_H

#include <stdint.h>

void    input_service_init(void);
void    input_service_process(void);

uint8_t input_get_pgood(void);
uint8_t input_get_sus_s3(void);
uint8_t input_get_faultz(void);
uint8_t input_get_in(uint8_t idx);

/* Packed byte for GET_STATUS: bit0..5=IN0..IN5, bit6=PGOOD, bit7=Faultz */
uint8_t input_get_packed(void);

#endif /* INPUT_SERVICE_H */
