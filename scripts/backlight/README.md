# Scripts: backlight over USB-UART

Control **BACKLIGHT** and brightness (**SET_BRIGHTNESS**) via USB-UART TTL 3.3 V on `PA9`/`PA10`, **115200 8N1**.

## Requirements

- Linux, `python3`, `xxd`, access to `/dev/ttyUSB*` (`dialout` group)
- Common GND with the board; MCU TX → adapter RX, MCU RX ← adapter TX

## Quick start

```bash
cd /path/to/POWER_Controller_BNT

# Port (default /dev/ttyUSB0)
export UART_DEVICE=/dev/ttyUSB0

chmod +x scripts/backlight/*.sh

# Link check
./scripts/backlight/bl.sh ping

# Domain state
./scripts/backlight/bl.sh status

# Brightness 50% (BL is usually already ON after boot)
./scripts/backlight/bl.sh set 50

# Max / min PWM
./scripts/backlight/bl.sh set pwm 1000
./scripts/backlight/bl.sh preset max

# Turn off backlight only
./scripts/backlight/backlight_off.sh
```

## `bl.sh` commands

| Command | Action |
| -------- | -------- |
| `ping` | PING → `0xAA` |
| `status` | GET_STATUS, domains |
| `on` | POWER_CTRL: BACKLIGHT ON |
| `on --with-display` | SCALER+LCD, then BACKLIGHT (full bench setup) |
| `off` | BACKLIGHT OFF |
| `set 75` / `set 75%` | SET_BRIGHTNESS, pwm = percent × 10 |
| `set pwm 500` | SET_BRIGHTNESS, pwm 0…1000 |
| `preset dim\|mid\|max` | 0 / 500 / 1000 |

Wrappers: `brightness.sh`, `backlight_on.sh`, `backlight_off.sh`.

## Typical scenarios

**Backlight already ON after MCU autostart** — `set` is enough:

```bash
./scripts/backlight/bl.sh set 30
./scripts/backlight/bl.sh set pwm 800
```

**BACKLIGHT was off** — run `on` first, then set brightness:

```bash
./scripts/backlight/bl.sh on --with-display
./scripts/backlight/bl.sh set 50
```

**Dim BL only**, leave display as is:

```bash
./scripts/backlight/backlight_off.sh
```

## Protocol (debug without scripts)

| Action | Hex frame |
| -------- | -------- |
| SET_BRIGHTNESS 50% | `02 03 02 F4 01 AB 03` |
| BACKLIGHT OFF | `02 02 04 04 00 00 00 D5 03` |
| BACKLIGHT ON | `02 02 04 04 00 04 00 D1 03` |

See also: [README.md](../../README.md), [Test_firmware.md](../../Test_firmware.md).
