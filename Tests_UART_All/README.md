# Bench UART tests — peripheral (display connected, no Q7)

Scripts for **POWER_Controller** on a bench with:

- board powered (12/5/3.3 V per schematic);
- **scaler, LCD, backlight** connected (real load);
- **USB-UART TTL 3.3 V** on **UART0** (`PA9`/`PA10`, common GND);
- **no Q7 (Linux)** — OTA and full Linux recovery flows are out of scope.

Protocol helpers: [`scripts/uart/`](../scripts/uart/). **Terminal log output is English.**

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
chmod +x *.sh
./run_all_peripheral.sh
```

## Test blocks

| Script | Block | Description |
|--------|-------|-------------|
| `22_get_version.sh` | — | `GET_VERSION` (`0x0A`): LEN=13 + CRC (invariant 52) |
| `02_reset_fault.sh` | — | RESET_FAULT + all domains OFF + `CALIBRATE_OFFSET` if currents ≫0 at state=0 |
| `23_neg_backlight_no_display.sh` | K | BACKLIGHT without SCALER/LCD → `status=0x01` |
| `24_neg_lcd_no_scaler.sh` | K | LCD without SCALER → `status=0x01` |
| `25_neg_scaler_backlight_no_lcd.sh` | K | SCALER\|BACKLIGHT without LCD → atomic reject |
| `03_display_scaler_lcd_on.sh` | 1 | `POWER_CTRL` SCALER+LCD ON, state `0x03`, rails in range |
| `04_telemetry_under_load.sh` | 2 | Non-zero load currents |
| `05_backlight_brightness.sh` | 1 | BACKLIGHT ON, `SET_BRIGHTNESS` 500 / 1000 / 0 |
| `26_set_brightness_no_bl.sh` | 1 | `SET_BRIGHTNESS` at `state=0x03` (BL off) — ACK OK, state unchanged |
| `27_set_brightness_neg.sh` | 1 | Bad LEN / pwm>1000 rejected with BL on |
| `28_bl_bor_diag.sh` | 1 | Two BL ON cycles clean — catches BL verify / PGOOD grace / MCU reset |
| `06_reset_bridge_display.sh` | 4 | `RESET_BRIDGE` with SCALER+LCD (LA on PB8) |
| `07_audio_sequencing.sh` | 5 | AUDIO ON/OFF — `[SKIP]` if `PERIPH_AUDIO_HW_ENABLED=0` |
| `08_display_shutdown.sh` | 1 | LCD OFF with BL ON → full shutdown, display bits clear |
| `11_backlight_only_off.sh` | 1 | `BL OFF` при активных `SCALER+LCD` → остаётся `state=0x03` |
| `12_all_at_once_up.sh` | 1 | `POWER_CTRL` с маской `0x0007` поднимает `SCALER+LCD+BL` из `state=0` |
| `14_set_brightness_boundary.sh` | 1 | `SET_BRIGHTNESS` на граничных значениях `1` и `999` при `BL ON` |
| `15_display_resequence.sh` | 1 | Повторные циклы `UP/DN/UP` дисплея без power-reset |
| `16_stress_get_status_load.sh` | K | 20× GET_STATUS при `state=0x07` |
| `17_iwdg_stress_load.sh` | I | 20× GET_STATUS (100 ms) под нагрузкой |
| `09_fault_lcd_current.sh` | 3 | `I_LCD_MAX=50` mA → `FAULT_LCD`, restore |
| `13_fault_recovery_display.sh` | 3 | `FAULT_LCD` latch → safe state → `RESET_FAULT` → display `ON` |
| `18_fault_v12_under_load.sh` | F | `FAULT_V12_RANGE` при включённом дисплее |
| `30_fault_v5_under_load.sh` | F | `FAULT_V5_RANGE` при включённом дисплее |
| `32_fault_v3v3_under_load.sh` | F | `FAULT_V3V3_RANGE` при включённом дисплее |
| `19_fault_scaler_current.sh` | 3 | `I_SCALER_MAX` trap → `FAULT_SCALER` |
| `20_fault_backlight_current.sh` | 3 | `I_BACKLIGHT_MAX` trap — `[SKIP]` if no BL current sensor |
| `21_fault_audio_current.sh` | 3 | `I_AUDIO_*` trap — `[SKIP]` if `PERIPH_AUDIO_HW_ENABLED=0` |
| `29_calibrate_offset_neg_display.sh` | — | `CALIBRATE_OFFSET` rejected at `state=0x03` |
| `31_simple_domains_periph.sh` | K | TOUCH/ETH toggle — `[SKIP]` if `PERIPH_TOUCH_HW_ENABLED=0` |
| `10_sus_s3_manual.sh` | 6 | SUS_S3# → PWRBTN# procedure (manual) |
| `33_pgood_mid_seq_manual.sh` | 6 | PGOOD drop mid-sequence → emergency off + fault (manual, inv. 44) |

Without flash calibration, current sensors often read **~1.5 A at state=0** — enabling SCALER/AUDIO can immediately latch `FAULT_SCALER` / `FAULT_AUDIO`. Scripts run `CALIBRATE_OFFSET` in `periph_prepare_zero_load` when needed.

If **NSM2012 (U4) is not populated** on the backlight path (`IP+`/`IP-` jumpered), firmware must use `ENABLE_BL_CURRENT_SENSOR=0` in `Config/config.h`. UART tests then expect `i_backlight=-32768` and omit that channel from load/zero current checks (`TELEMETRY_I_CHANNELS`, `LOAD_I_CHANNELS` in `config.sh`).

If **`03_display_scaler_lcd_on.sh`** fails with **fault `0x2001`** (SCALER verify), `run_all_peripheral.sh` marks display-dependent scripts as **`[SKIP]`** instead of cascading **FAIL**. Tests **07** (audio), **22** (GET_VERSION), **23–25** (policy neg), **10** / **33** (manual) still run.

Bench fault traps cover `FAULT_SCALER` / `FAULT_LCD` / `FAULT_V12|V5|V3V3_RANGE` and optionally `FAULT_BACKLIGHT` / `FAULT_AUDIO`. Bits `FAULT_ETH1/2`, `FAULT_TOUCH`, `FAULT_PGOOD_LOST`, `FAULT_AMP_FAULTZ`, `FAULT_SEQ_ABORT` (except via **33** manual) are covered by host unit tests, not ADC benches. `FAULT_V24_RANGE` has no trap helpers yet.

## Environment variables

| Variable | Default | Purpose |
|----------|---------|---------|
| `UART_DEVICE` | `/dev/ttyUSB0` | Serial port |
| `SEQ_ON_WAIT_SEC` | `2.0` | Delay after SCALER+LCD ON |
| `THRESH_I_LCD_TRAP_MA` | `50` | LCD overcurrent trap for block 3 |
| `LOAD_I_MIN_MA` | `5` | Minimum plausible load current |
| `PERIPH_AUDIO_HW_ENABLED` | `0` | `0` — AUDIO ON ожидаемо отклоняется (status=1); `1` — полный AUDIO + `21_fault_audio_current.sh` |
| `PERIPH_TOUCH_HW_ENABLED` | `0` | `1` — `31_simple_domains_periph.sh` (TOUCH/ETH) |
| `PERIPH_BL_CURRENT_SENSOR_ENABLED` | `0` | `1` — `20_fault_backlight_current.sh` (NSM2012 on BL shunt) |
| `THRESH_I_SCALER_TRAP_MA` | `50` | Scaler overcurrent trap (block 3) |
| `THRESH_I_BL_TRAP_MA` | `50` | Backlight overcurrent trap |
| `RUN_SUS_S3_HW_TEST` | `0` | `1` — count block 6 as PASS after printing steps |
| `RUN_PGOOD_MID_SEQ_HW_TEST` | `0` | `1` — count PGOOD mid-seq manual as PASS after printing steps |
| `DISPLAY_SKIP_REASON` | (built-in EN string) | Message when display tests are skipped |

## Manual checks (not over UART)

- **PB1 / PB0 / PB9** — domain voltages and backlight PWM (scope)
- **PB8** — `RESET_BRIDGE` pulse
- **PC15 / PC14** — SUS_S3# hold and PWRBTN# pulse
- **PGOOD** — pull LOW mid display sequence (see `33_pgood_mid_seq_manual.sh`)
- Compare currents with **DMM in series** if the bench allows

## Make targets

- `make test-uart-all` — this directory (display bench)
