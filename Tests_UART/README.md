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
| `02_get_status.sh` | C.2 | 31-byte frame, `state=0`, `fault=0` |
| `04_neg_backlight.sh` | C.7 | BACKLIGHT without SCALER/LCD → `0x01` |
| `12_stress_get_status.sh` | K.1 | 20× GET_STATUS |

See table in repo for full list.

## Not in bare-board run

- Successful `POWER_CTRL` SCALER/LCD/BACKLIGHT (needs display)
- `SET_BRIGHTNESS`, `BOOTLOADER_ENTER`, `CALIBRATE_OFFSET`
- SUS_S3# / PWRBTN (needs Q7 or LA)

After test **#13**, run `./03_reset_fault.sh` before `run_all_bare_board.sh`.

## Wiring

`Test_firmware.md` §4.3: **115200 8N1**, MCU TX `PA9` → adapter RX, MCU RX `PA10` ← adapter TX, **GND**.
