#ifndef FAULT_MANAGER_H
#define FAULT_MANAGER_H

#include <stdint.h>

void     fault_manager_init(void);
void     fault_manager_process(void);

uint16_t fault_get_flags(void);
void     fault_clear_flags(void);
void     fault_set_flag(uint16_t flag);
void     fault_set_threshold(uint8_t idx, uint16_t min_val, uint16_t max_val);

#endif /* FAULT_MANAGER_H */
