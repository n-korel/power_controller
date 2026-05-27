# Bench UART tests — peripheral (display connected, no Q7)

Scripts for **POWER_Controller** on a bench with:

- board powered (12/5/3.3 V per schematic);
- **scaler, LCD, backlight, audio** connected (real load);
- **USB-UART TTL 3.3 V** on **UART0** (`PA9`/`PA10`, common GND);
- **no Q7 (Linux)** — OTA and full Linux recovery flows are out of scope.

Protocol helpers inherit from [`Tests_UART/`](../Tests_UART/). **Terminal log output is English.**

## Requirements

- Linux, `bash`, `python3`, `xxd`, `timeout`, `stty`
- Access to `/dev/ttyUSB*` (`dialout` group)
- **PGOOD = HIGH** (otherwise display sequencing and block 6 do not apply)

## Quick start

From repo root:

```bash
make test-uart-all
make test-uart-all UART_DEVICE=/dev/ttyACM0
```

Or directly:

```bash
cd Tests_UART_All
chmod +x *.sh parse_get_status.py
./run_all_peripheral.sh
```

## Test blocks

| Script | Block | Description |
|--------|-------|-------------|
| `02_reset_fault.sh` | — | RESET_FAULT + all domains OFF + `CALIBRATE_OFFSET` if currents ≫0 at state=0 |
| `03_display_scaler_lcd_on.sh` | 1 | `POWER_CTRL` SCALER+LCD ON, state `0x03`, rails in range |
| `05_backlight_brightness.sh` | 1 | BACKLIGHT ON, `SET_BRIGHTNESS` 500 / 1000 / 0 |
| `08_display_shutdown.sh` | 1 | LCD OFF with BL ON → full shutdown, display bits clear |
| `04_telemetry_under_load.sh` | 2 | Non-zero load currents |
| `09_fault_lcd_current.sh` | 3 | `I_LCD_MAX=50` mA → `FAULT_LCD`, restore |
| `11_backlight_only_off.sh` | 1 | `BL OFF` при активных `SCALER+LCD` → остаётся `state=0x03` |
| `12_all_at_once_up.sh` | 1 | `POWER_CTRL` с маской `0x0007` поднимает `SCALER+LCD+BL` из `state=0` |
| `13_fault_recovery_display.sh` | 3 | `FAULT_LCD` latch → safe state → `RESET_FAULT` → display `ON` |
| `14_set_brightness_boundary.sh` | 1 | `SET_BRIGHTNESS` на граничных значениях `1` и `999` при `BL ON` |
| `15_display_resequence.sh` | 1 | Повторные циклы `UP/DN/UP` дисплея без power-reset |
| `06_reset_bridge_display.sh` | 4 | `RESET_BRIDGE` with SCALER+LCD (LA on PB8) |
| `07_audio_sequencing.sh` | 5 | AUDIO ON/OFF (DMM: PC8 SDZ, PC6 MUTE) |
| `10_sus_s3_manual.sh` | 6 | SUS_S3# → PWRBTN# procedure (manual) |

Without flash calibration, current sensors often read **~1.5 A at state=0** — enabling SCALER/AUDIO can immediately latch `FAULT_SCALER` / `FAULT_AUDIO`. Scripts run `CALIBRATE_OFFSET` in `periph_prepare_zero_load` when needed.

If **`03_display_scaler_lcd_on.sh`** fails with **fault `0x2001`** (SCALER verify), `run_all_peripheral.sh` marks **04–06, 08–09, 11–15** as **`[SKIP]`** instead of cascading **FAIL**. Tests **07** (audio) and **10** (SUS_S3 manual) still run.

## Environment variables

| Variable | Default | Purpose |
|----------|---------|---------|
| `UART_DEVICE` | `/dev/ttyUSB0` | Serial port |
| `SEQ_ON_WAIT_SEC` | `2.0` | Delay after SCALER+LCD ON |
| `THRESH_I_LCD_TRAP_MA` | `50` | LCD overcurrent trap for block 3 |
| `LOAD_I_MIN_MA` | `5` | Minimum plausible load current |
| `RUN_SUS_S3_HW_TEST` | `0` | `1` — count block 6 as PASS after printing steps |
| `DISPLAY_SKIP_REASON` | (built-in EN string) | Message when display tests are skipped |

## Manual checks (not over UART)

- **PB1 / PB0 / PB9** — domain voltages and backlight PWM (scope)
- **PB8** — `RESET_BRIDGE` pulse
- **PC15 / PC14** — SUS_S3# hold and PWRBTN# pulse
- Compare currents with **DMM in series** if the bench allows

## Make targets

- `make test-uart` — bare board, [`Tests_UART/`](../Tests_UART/)
- `make test-uart-all` — this directory
