# Bench UART tests (bare board)

Scripts for **POWER_Controller** on a minimal setup:

- powered board (12/5/3.3 V per schematic);
- **USB-UART TTL 3.3 V** on **UART0** (`PA9`/`PA10`, common GND);
- **no Q7 (Linux)** and **no display**.

Spec: `Test_firmware.md` (stages **B/C**, **K.1**).  
`POWER_CTRL` without display: `13_optional_power_ctrl_seq_fault.sh`.

## Requirements

- Linux, `bash`, `python3`, `xxd`, `timeout`, `stty`
- Access to `/dev/ttyUSB*` (`dialout` group)

## Quick start

From repo root:

```bash
make test-uart
make test-uart UART_DEVICE=/dev/ttyACM0
# longer preflight after cold boot: BOOT_PING_RETRIES=90 make test-uart
```

Or directly:

```bash
cd Tests_UART
chmod +x *.sh parse_get_status.py
./run_all_bare_board.sh
```

## Scripts

| File | Test_firmware ID | Description |
|------|------------------|-------------|
| `run_all_bare_board.sh` | — | Full suite (excludes #13) |
| `01_ping.sh` | C.1 | PING → `0xAA` |
| `03_reset_fault.sh` | C.11 | Clears latched faults before clean GET_STATUS |
| `02_get_status.sh` | C.2 | 34-byte frame, `state=0`, `fault=0` |
| `04_neg_backlight.sh` | C.7 | BACKLIGHT without SCALER/LCD → `0x01` |
| `12_stress_get_status.sh` | K.1 | 20× GET_STATUS (50 ms gap) |
| `14_neg_scaler_backlight.sh` | K.3 | SCALER\|BACKLIGHT without LCD → `0x01` |
| `15_simple_domains.sh` | K.5 | TOUCH / ETH1 / ETH2 toggle |
| `16_telemetry_sanity.sh` | A.2, A.3 | Rails, zero-load currents, NTC absent |
| `17_fault_v12_range.sh` | F.1 | SET_THRESHOLDS → `FAULT_V12_RANGE`, restore |
| `18_fault_reserved.sh` | F.3 | Bit 15 never set under fault |
| `19_iwdg_stress.sh` | I.1 | 20× GET_STATUS (100 ms gap) |
| `20_set_brightness_neg.sh` | — | SET_BRIGHTNESS bad LEN, pwm>1000 |
| `21_power_ctrl_neg.sh` | — | POWER_CTRL bad LEN, unknown bits |
| `22_calibrate_offset_neg_state.sh` | — | CALIBRATE_OFFSET rejected when `state!=0` |
| `23_set_thresholds_neg.sh` | — | SET_THRESHOLDS invalid mask / min≥max / truncated |
| `24_fault_v5_range.sh` | F.1 | SET_THRESHOLDS → FAULT_V5_RANGE |
| `25_fault_v3v3_range.sh` | F.1 | SET_THRESHOLDS → FAULT_V3V3_RANGE |
| `26_verify_rx_crc.sh` | — | CRC on PING and GET_STATUS responses |
| `27_power_ctrl_idempotent.sh` | — | TOUCH ON repeated: `status=0`, state unchanged |
| `28_set_thresholds_pos.sh` | — | Valid `SET_THRESHOLDS` (V12 + I_LCD) and restore defaults |
| `29_reset_fault_no_autostart.sh` | — | Fault latch + RESET_FAULT: `state=0`, `fault=0` |
| `30_power_ctrl_zero_mask.sh` | — | `POWER_CTRL mask=0,value=0` no-op, state unchanged |
| `31_set_brightness_valid.sh` | — | `SET_BRIGHTNESS` 0/500/1000 accepted |

Not in `make test-uart`: bootloader (`stm32flash`), optional `#13`.

## Not in bare-board run

- Successful `POWER_CTRL` SCALER/LCD/BACKLIGHT (needs display)
- `BOOTLOADER_ENTER`, `CALIBRATE_OFFSET`
- SUS_S3# / PWRBTN (needs Q7 or LA)

`run_all_bare_board.sh`: `00_flush_port` → **preflight PING retries** (до ${BOOT_PING_RETRIES:-60}×, cold boot) → tests (`03_reset_fault` after `01_ping`, …). После optional **#13** снова `./03_reset_fault.sh`.

## Wiring

`Test_firmware.md` §4.3: **115200 8N1**, MCU TX `PA9` → adapter RX, MCU RX `PA10` ← adapter TX, **GND**.
