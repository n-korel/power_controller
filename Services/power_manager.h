#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdint.h>

void    power_manager_init(void);
void    power_manager_process(void);

uint8_t power_get_state(void);
/* Display sequencer phase (internal `dseq` enum cast to uint8_t). Idle=0.
 * Exposed last in GET_STATUS for bench UART debug. */
uint8_t power_get_dseq_raw(void);
/* Last POWER_CTRL request payload (low byte only, domain bits 0..6). */
uint8_t power_get_last_power_ctrl_mask_lo(void);
uint8_t power_get_last_power_ctrl_value_lo(void);
/* Raw RCC reset flags snapshot (RCC->CSR) captured once at boot. */
uint32_t power_get_reset_flags_raw(void);
/* Monotonic boot counter stored in .noinit (retained across resets). */
uint32_t power_get_boot_counter(void);
/* Non-zero when display and audio sequencers are idle. */
uint8_t power_is_idle(void);
uint8_t power_ctrl_request(uint16_t mask, uint16_t value);

void    power_set_brightness(uint16_t pwm);
uint8_t power_reset_bridge(void);
void    power_safe_state(void);

/* Called by fault_manager to force-off specific domains */
void    power_force_off_domains(uint16_t domain_mask);

/* Emergency display shutdown (no sequencing delays) */
void    power_emergency_display_off(void);

/* Arm the startup state machine (non-blocking PGOOD wait).
 * Once armed, power_manager_process() polls PGOOD each iteration:
 *   - PGOOD=HIGH -> run internal auto-startup (Rules 6.1 / 6.5)
 *   - timeout (PGOOD_TIMEOUT_MS) -> latch FAULT_PGOOD_LOST
 * Must be called exactly once after power_manager_init() in main(). */
void    power_startup_begin(void);

#endif /* POWER_MANAGER_H */
