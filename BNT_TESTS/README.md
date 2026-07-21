# BNT_TESTS — UART tests on Q7 (Linux)

Pure BusyBox `ash` + `socat` (no bash/python). Default port: `/dev/ttyS0`.

## Requirements on Q7

- `socat`, `xxd`, `awk`, `stty`, `seq`
- access to MCU UART0 (usually `/dev/ttyS0`)

## Copy to Q7

Корень на Q7 часто read-only — кладите в `/opt` или `/tmp`:

```bash
scp -r BNT_TESTS root@<Q7>:/opt/
# или: scp -r BNT_TESTS root@<Q7>:/tmp/
```

## Run

```bash
cd /opt/BNT_TESTS   # или /tmp/BNT_TESTS
chmod +x *.sh

# smoke
./01_ping.sh
./22_get_version.sh

# full suite
./run_all.sh

# port (default already /dev/ttyS0)
UART_DEVICE=/dev/ttyS0 ./run_all.sh
```

Manual one-shot (same transport as tests):

```bash
printf '\x02\x01\x00\x15\x03' | socat -t1 - \
  FILE:/dev/ttyS0,b115200,cs8,parenb=0,cstopb=0,raw,echo=0,crtscts=0 | xxd
# expect: 02 01 01 aa ... 03
```

## Notes

- Same scenarios as `Tests_UART_All/` (display peripheral suite).
- AUDIO ON current diag (`36_audio_on_current_diag.sh`) — отдельно, не в `run_all.sh`.
  Ловит пики `i_audio_l/r` и момент latch `FAULT_AUDIO` / `FAULT_AMP_FAULTZ` сразу после ON
  (без `POWER_CTRL_TX_DELAY`). Пример:

```bash
./36_audio_on_current_diag.sh
AUDIO_DIAG_WITH_DISPLAY=1 AUDIO_DIAG_SAMPLES=60 ./36_audio_on_current_diag.sh
```

- OTA pending-confirm (`34` / `35`) — отдельно, не в `run_all.sh`. Нужны `stm32flash`, `gpioset` (IC17) и образ.

После `stm32flash -g` скрипт сам делает **NRST с BOOT0=0** через IC17 (`gpiochip5` line8/9) — иначе приложение не отвечает на PING. Отключить: `OTA_POST_FLASH_NRST=0`. Свой импульс: `OTA_NRST_CMD='...'`.

```bash
scp build/POWER_Controller_BNT.bin root@<Q7>:/opt/BNT_STM32/
cd /opt/BNT_STM32/BNT_TESTS
export PATH="/opt/stm32flash:$PATH"
FW_BIN=/opt/BNT_STM32/POWER_Controller_BNT.bin ./34_ota_confirm_reset_fault.sh
# safe-hold: 3× NRST <10s, без BOOTLOADER_ENTER между boot-ами:
FW_BIN=/opt/BNT_STM32/POWER_Controller_BNT.bin ./35_ota_unconfirmed_safe_hold.sh
```
