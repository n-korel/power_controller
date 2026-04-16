#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdint.h>

void    power_manager_init(void);
void    power_manager_process(void);

uint8_t power_get_state(void);
uint8_t power_ctrl_request(uint16_t mask, uint16_t value);

void    power_set_brightness(uint16_t pwm);
void    power_reset_bridge(void);
void    power_safe_state(void);

/* Called by fault_manager to force-off specific domains */
void    power_force_off_domains(uint16_t domain_mask);

/* Emergency display shutdown (no sequencing delays) */
void    power_emergency_display_off(void);

/* Auto-startup sequence after PGOOD */
void    power_auto_startup(void);

#endif /* POWER_MANAGER_H */
