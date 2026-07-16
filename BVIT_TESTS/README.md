# BVIT_TESTS — UART tests on Q7 (Linux)

Pure BusyBox `ash` + `socat` (no bash/python). Default port: `/dev/ttyS0`.

## Requirements on Q7

- `socat`, `xxd`, `awk`, `stty`, `seq`
- access to MCU UART0 (usually `/dev/ttyS0`)

## Copy to Q7

Корень на Q7 часто read-only — кладите в `/opt` или `/tmp`:

```bash
scp -r BVIT_TESTS root@<Q7>:/opt/
# или: scp -r BVIT_TESTS root@<Q7>:/tmp/
```

## Run

```bash
cd /opt/BVIT_TESTS   # или /tmp/BVIT_TESTS
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
- Manual scripts `10_sus_s3_manual.sh` / `33_pgood_mid_seq_manual.sh` default to `[SKIP]`.
- OTA pending-confirm (`34` / `35`) — отдельно, не в `run_all.sh`. Нужны `stm32flash` и образ:

```bash
scp build/POWER_Controller.bin root@<Q7>:/tmp/
cd /opt/BVIT_TESTS
FW_BIN=/tmp/POWER_Controller.bin ./34_ota_confirm_reset_fault.sh
# safe-hold: 3× NRST <10s (IC17), без BOOTLOADER_ENTER между boot-ами:
OTA_NRST_CMD='your-ic17-nrst-only.sh' FW_BIN=/tmp/POWER_Controller.bin \
  ./35_ota_unconfirmed_safe_hold.sh
```

`OTA_NRST_CMD` — только импульс NRST (BOOT0=0), иначе `BOOTLOADER_ENTER` снова армит pending.
