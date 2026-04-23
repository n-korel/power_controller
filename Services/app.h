#ifndef APP_H
#define APP_H

/*
 * Application glue for main.c:
 * - app_init(): user initialization after CubeMX init
 * - app_step(): one iteration of the main-loop work (without IWDG refresh)
 *
 * IWDG refresh stays in main.c per Rules_POWER.md #48-#50.
 */

void app_init(void);
void app_step(void);

#endif /* APP_H */

