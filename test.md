nikita-korelsky@korelsky-KSC-PC:~/Documents/POWER_Controller/POWER_Controller$ make test-uart-all
UART_DEVICE=/dev/ttyUSB0 bash Tests_UART_All/run_all_peripheral.sh

=== POWER_Controller UART — peripheral (display, no Q7) ===
Port: /dev/ttyUSB0

--- 00_flush_port.sh ---
[PASS] Port /dev/ttyUSB0 ready (115200 8N1 raw)

[INFO] Waiting for MCU UART (up to 60 PING, 0.5s interval)...
[INFO] MCU UART probe attempt 1/60
[PASS] MCU UART ready (attempt 1/60)

--- 01_ping.sh ---
[PASS] PING → 0xAA

--- 02_reset_fault.sh ---
[INFO] all domains off: mask=0x007F value=0
FAIL: i_lcd=-1518 mA not in [-200,200]
FAIL: i_scaler=-1924 mA not in [-200,200]
FAIL: i_audio_l=-1439 mA not in [-200,200]
FAIL: i_audio_r=-1833 mA not in [-200,200]
[INFO] CALIBRATE_OFFSET (zero load, state=0; else spurious FAULT_SCALER/AUDIO)
currents OK
[PASS] CALIBRATE_OFFSET: zero-load currents OK
[PASS] baseline: state=0 fault=0, currents calibrated

--- 03_display_scaler_lcd_on.sh ---
[INFO] all domains off: mask=0x007F value=0
FAIL: i_lcd=208 mA not in [-200,200]
[INFO] CALIBRATE_OFFSET (zero load, state=0; else spurious FAULT_SCALER/AUDIO)
currents OK
[PASS] CALIBRATE_OFFSET: zero-load currents OK
[INFO] POWER_CTRL SCALER+LCD ON (mask=0x0003 value=0x0003)
rails OK
[INFO] verify: SCALER_POWER_M (PB1) and LCD rail — use LA/DMM if needed (not in GET_STATUS)
[PASS] display: SCALER+LCD ON, state=0x03, fault=0, rails OK

--- 04_telemetry_under_load.sh ---
[INFO] all domains off: mask=0x007F value=0
FAIL: i_lcd=287 mA not in [-200,200]
FAIL: i_scaler=253 mA not in [-200,200]
FAIL: i_audio_l=250 mA not in [-200,200]
FAIL: i_audio_r=246 mA not in [-200,200]
[INFO] CALIBRATE_OFFSET (zero load, state=0; else spurious FAULT_SCALER/AUDIO)
currents OK
[PASS] CALIBRATE_OFFSET: zero-load currents OK
[INFO] POWER_CTRL SCALER+LCD ON (mask=0x0003 value=0x0003)
v24=0
v12=11719
v5=5189
v3v3=3396
i_lcd=143
i_backlight=-32768
i_scaler=136
i_audio_l=132
i_audio_r=132
temp0=-32768
temp1=-32768
state=0x03
fault_flags=0x0000
inputs=0xff
dseq=0
last_power_ctrl_mask_lo=0x03
last_power_ctrl_value_lo=0x03
reset_flags_raw=0x0c800003
boot_counter=474714862
pgood=1
currents OK
[PASS] load currents in [5,3200] mA for i_lcd,i_scaler
[INFO] compare with DMM in series if your bench allows

--- 05_backlight_brightness.sh ---
[INFO] all domains off: mask=0x007F value=0
FAIL: i_lcd=238 mA not in [-200,200]
FAIL: i_scaler=231 mA not in [-200,200]
FAIL: i_audio_l=223 mA not in [-200,200]
FAIL: i_audio_r=223 mA not in [-200,200]
[INFO] CALIBRATE_OFFSET (zero load, state=0; else spurious FAULT_SCALER/AUDIO)
currents OK
[PASS] CALIBRATE_OFFSET: zero-load currents OK
[INFO] POWER_CTRL SCALER+LCD ON (mask=0x0003 value=0x0003)
[INFO] SCALER+LCD already on
[INFO] POWER_CTRL BACKLIGHT ON (mask=0x0004 value=0x0004), attempt 1/3
[INFO] --- GET_STATUS (BACKLIGHT ON attempt 1 failed) ---
v24=0
v12=11719
v5=5189
v3v3=3403
i_lcd=553
i_backlight=-32768
i_scaler=594
i_audio_l=594
i_audio_r=598
temp0=-32768
temp1=-32768
state=0x03
fault_flags=0x0000
inputs=0xff
dseq=0
last_power_ctrl_mask_lo=0x00
last_power_ctrl_value_lo=0x00
reset_flags_raw=0x0c800003
boot_counter=483365614
pgood=1
[INFO] SCALER+LCD already on
[INFO] POWER_CTRL BACKLIGHT ON (mask=0x0004 value=0x0004), attempt 2/3
[INFO] --- GET_STATUS (BACKLIGHT ON attempt 2 failed) ---
v24=0
v12=11705
v5=5189
v3v3=3396
i_lcd=678
i_backlight=-32768
i_scaler=867
i_audio_l=871
i_audio_r=886
temp0=-32768
temp1=-32768
state=0x03
fault_flags=0x0000
inputs=0xff
dseq=0
last_power_ctrl_mask_lo=0x00
last_power_ctrl_value_lo=0x00
reset_flags_raw=0x0c800003
boot_counter=474718946
pgood=1
[INFO] SCALER+LCD already on
[INFO] POWER_CTRL BACKLIGHT ON (mask=0x0004 value=0x0004), attempt 3/3
[INFO] --- GET_STATUS (BACKLIGHT ON attempt 3 failed) ---
v24=0
v12=11726
v5=5196
v3v3=3403
i_lcd=696
i_backlight=-32768
i_scaler=1022
i_audio_l=1037
i_audio_r=1064
temp0=-32768
temp1=-32768
state=0x03
fault_flags=0x0000
inputs=0xff
dseq=0
last_power_ctrl_mask_lo=0x00
last_power_ctrl_value_lo=0x00
reset_flags_raw=0x0c800003
boot_counter=483111654
pgood=1
[FAIL] BACKLIGHT ON failed

--- 06_reset_bridge_display.sh ---
[INFO] all domains off: mask=0x007F value=0
FAIL: i_lcd=723 mA not in [-200,200]
FAIL: i_scaler=1034 mA not in [-200,200]
FAIL: i_audio_l=1056 mA not in [-200,200]
FAIL: i_audio_r=1083 mA not in [-200,200]
[INFO] CALIBRATE_OFFSET (zero load, state=0; else spurious FAULT_SCALER/AUDIO)
currents OK
[PASS] CALIBRATE_OFFSET: zero-load currents OK
[INFO] POWER_CTRL SCALER+LCD ON (mask=0x0003 value=0x0003)
[INFO] RESET_BRIDGE (~10 ms LOW RST_CH7511B / PB8 — verify with LA)
[PASS] RESET_BRIDGE: ACK 0x00, no fault latched

--- 07_audio_sequencing.sh ---
[INFO] all domains off: mask=0x007F value=0
currents OK
[INFO] current offsets OK (±200 mA at state=0)
[INFO] POWER_CTRL AUDIO ON must be rejected on this revision (mask=0x0008 value=0x0008)
[INFO] POWER_CTRL AUDIO OFF (mask=0x0008 value=0x0000)
[PASS] audio sequencing: revision-disabled AUDIO is rejected as expected

--- 08_display_shutdown.sh ---
[INFO] all domains off: mask=0x007F value=0
currents OK
[INFO] current offsets OK (±200 mA at state=0)
[INFO] POWER_CTRL SCALER+LCD ON (mask=0x0003 value=0x0003)
[INFO] SCALER+LCD already on
[INFO] POWER_CTRL BACKLIGHT ON (mask=0x0004 value=0x0004), attempt 1/3
[INFO] --- GET_STATUS (BACKLIGHT ON attempt 1 failed) ---
v24=0
v12=11691
v5=5189
v3v3=3403
i_lcd=-18
i_backlight=-32768
i_scaler=113
i_audio_l=128
i_audio_r=-102
temp0=-32768
temp1=-32768
state=0x03
fault_flags=0x0000
inputs=0xff
dseq=0
last_power_ctrl_mask_lo=0x00
last_power_ctrl_value_lo=0x00
reset_flags_raw=0x0c800003
boot_counter=483107566
pgood=1
[INFO] SCALER+LCD already on
[INFO] POWER_CTRL BACKLIGHT ON (mask=0x0004 value=0x0004), attempt 2/3
[INFO] --- GET_STATUS (BACKLIGHT ON attempt 2 failed) ---
v24=0
v12=11726
v5=5189
v3v3=3403
i_lcd=-26
i_backlight=-32768
i_scaler=143
i_audio_l=166
i_audio_r=-215
temp0=-32768
temp1=-32768
state=0x03
fault_flags=0x0000
inputs=0xff
dseq=0
last_power_ctrl_mask_lo=0x00
last_power_ctrl_value_lo=0x00
reset_flags_raw=0x0c800003
boot_counter=474714862
pgood=1
[INFO] SCALER+LCD already on
[INFO] POWER_CTRL BACKLIGHT ON (mask=0x0004 value=0x0004), attempt 3/3
[INFO] --- GET_STATUS (BACKLIGHT ON attempt 3 failed) ---
v24=0
v12=11712
v5=5196
v3v3=3403
i_lcd=-22
i_backlight=-32768
i_scaler=162
i_audio_l=-94
i_audio_r=-64
temp0=-32768
temp1=-32768
state=0x03
fault_flags=0x0000
inputs=0xff
dseq=0
last_power_ctrl_mask_lo=0x00
last_power_ctrl_value_lo=0x00
reset_flags_raw=0x0c800003
boot_counter=407601902
pgood=1
[FAIL] setup: BACKLIGHT ON

--- 09_fault_lcd_current.sh ---
[INFO] all domains off: mask=0x007F value=0
currents OK
[INFO] current offsets OK (±200 mA at state=0)
[INFO] POWER_CTRL SCALER+LCD ON (mask=0x0003 value=0x0003)
[INFO] trap I_LCD max=50 mA (default 2000)
v24=0
v12=12031
v5=5196
v3v3=3403
i_lcd=49
i_backlight=-32768
i_scaler=166
i_audio_l=-159
i_audio_r=-11
temp0=-32768
temp1=-32768
state=0x00
fault_flags=0x0002
inputs=0xff
dseq=0
last_power_ctrl_mask_lo=0x03
last_power_ctrl_value_lo=0x03
reset_flags_raw=0x0c800003
boot_counter=407601902
pgood=1
[PASS] I_LCD overcurrent: FAULT_LCD latched, thresholds restored

--- 11_backlight_only_off.sh ---
[INFO] all domains off: mask=0x007F value=0
currents OK
[INFO] current offsets OK (±200 mA at state=0)
[INFO] POWER_CTRL SCALER+LCD ON (mask=0x0003 value=0x0003)
[INFO] POWER_CTRL BACKLIGHT ON (mask=0x0004 value=0x0004), attempt 1/3
[INFO] --- GET_STATUS (BACKLIGHT ON attempt 1 failed) ---
v24=0
v12=11719
v5=5189
v3v3=3403
i_lcd=-18
i_backlight=-32768
i_scaler=178
i_audio_l=-265
i_audio_r=56
temp0=-32768
temp1=-32768
state=0x03
fault_flags=0x0000
inputs=0xff
dseq=0
last_power_ctrl_mask_lo=0x00
last_power_ctrl_value_lo=0x00
reset_flags_raw=0x0c800003
boot_counter=474714862
pgood=1
[INFO] SCALER+LCD already on
[INFO] POWER_CTRL BACKLIGHT ON (mask=0x0004 value=0x0004), attempt 2/3
[INFO] --- GET_STATUS (BACKLIGHT ON attempt 2 failed) ---
v24=0
v12=11719
v5=5189
v3v3=3403
i_lcd=-26
i_backlight=-32768
i_scaler=178
i_audio_l=-295
i_audio_r=68
temp0=-32768
temp1=-32768
state=0x03
fault_flags=0x0000
inputs=0xff
dseq=0
last_power_ctrl_mask_lo=0x00
last_power_ctrl_value_lo=0x00
reset_flags_raw=0x0c800003
boot_counter=474714862
pgood=1
[INFO] SCALER+LCD already on
[INFO] POWER_CTRL BACKLIGHT ON (mask=0x0004 value=0x0004), attempt 3/3
[INFO] --- GET_STATUS (BACKLIGHT ON attempt 3 failed) ---
v24=0
v12=11719
v5=5189
v3v3=3403
i_lcd=-26
i_backlight=-32768
i_scaler=178
i_audio_l=-306
i_audio_r=71
temp0=-32768
temp1=-32768
state=0x03
fault_flags=0x0000
inputs=0xff
dseq=0
last_power_ctrl_mask_lo=0x00
last_power_ctrl_value_lo=0x00
reset_flags_raw=0x0c800003
boot_counter=474710762
pgood=1
[FAIL] setup: BACKLIGHT ON

--- 12_all_at_once_up.sh ---
[INFO] all domains off: mask=0x007F value=0
FAIL: i_audio_l=-310 mA not in [-200,200]
[INFO] CALIBRATE_OFFSET (zero load, state=0; else spurious FAULT_SCALER/AUDIO)
currents OK
[PASS] CALIBRATE_OFFSET: zero-load currents OK
[INFO] POWER_CTRL display all ON (mask=0x0007 value=0x0007)
[FAIL] expected state=0x07 after all-at-once UP

--- 13_fault_recovery_display.sh ---
[INFO] all domains off: mask=0x007F value=0
currents OK
[INFO] current offsets OK (±200 mA at state=0)
[INFO] POWER_CTRL SCALER+LCD ON (mask=0x0003 value=0x0003)
[INFO] trap I_LCD max=50 mA
[INFO] POWER_CTRL display all ON after fault reset
[FAIL] expected state=0x07 after recovery POWER_CTRL

--- 14_set_brightness_boundary.sh ---
[INFO] all domains off: mask=0x007F value=0
currents OK
[INFO] current offsets OK (±200 mA at state=0)
[INFO] POWER_CTRL SCALER+LCD ON (mask=0x0003 value=0x0003)
[INFO] POWER_CTRL BACKLIGHT ON (mask=0x0004 value=0x0004), attempt 1/3
[INFO] --- GET_STATUS (BACKLIGHT ON attempt 1 failed) ---
v24=0
v12=11719
v5=5189
v3v3=3403
i_lcd=-30
i_backlight=-32768
i_scaler=7
i_audio_l=-7
i_audio_r=-3
temp0=-32768
temp1=-32768
state=0x03
fault_flags=0x0000
inputs=0xff
dseq=0
last_power_ctrl_mask_lo=0x00
last_power_ctrl_value_lo=0x00
reset_flags_raw=0x0c800003
boot_counter=483103470
pgood=1
[INFO] SCALER+LCD already on
[INFO] POWER_CTRL BACKLIGHT ON (mask=0x0004 value=0x0004), attempt 2/3
[INFO] --- GET_STATUS (BACKLIGHT ON attempt 2 failed) ---
v24=0
v12=11726
v5=5196
v3v3=3403
i_lcd=-30
i_backlight=-32768
i_scaler=7
i_audio_l=-3
i_audio_r=-3
temp0=-32768
temp1=-32768
state=0x03
fault_flags=0x0000
inputs=0xff
dseq=0
last_power_ctrl_mask_lo=0x00
last_power_ctrl_value_lo=0x00
reset_flags_raw=0x0c800003
boot_counter=482583278
pgood=1
[INFO] SCALER+LCD already on
[INFO] POWER_CTRL BACKLIGHT ON (mask=0x0004 value=0x0004), attempt 3/3
[INFO] --- GET_STATUS (BACKLIGHT ON attempt 3 failed) ---
v24=0
v12=11719
v5=5189
v3v3=3396
i_lcd=-30
i_backlight=-32768
i_scaler=7
i_audio_l=-7
i_audio_r=-3
temp0=-32768
temp1=-32768
state=0x03
fault_flags=0x0000
inputs=0xff
dseq=0
last_power_ctrl_mask_lo=0x00
last_power_ctrl_value_lo=0x00
reset_flags_raw=0x0c800003
boot_counter=407601902
pgood=1
[FAIL] setup: BACKLIGHT ON

--- 15_display_resequence.sh ---
[INFO] all domains off: mask=0x007F value=0
currents OK
[INFO] current offsets OK (±200 mA at state=0)
[INFO] initial UP (display all ON)
[FAIL] initial UP: expected state=0x07

--- 10_sus_s3_manual.sh ---
v24=0
v12=11719
v5=5189
v3v3=3403
i_lcd=-26
i_backlight=-32768
i_scaler=7
i_audio_l=-7
i_audio_r=-3
temp0=-32768
temp1=-32768
state=0x03
fault_flags=0x0000
inputs=0xff
dseq=0
last_power_ctrl_mask_lo=0x00
last_power_ctrl_value_lo=0x00
reset_flags_raw=0x0c800003
boot_counter=483099374
pgood=1

=== Block 6: SUS_S3# -> PWRBTN# (manual check) ===

Prerequisites:

- PGOOD = HIGH (confirmed via GET_STATUS)
- Without Q7: SUS_S3# (PC15) is usually HIGH via R206 to VMCU

Steps:

1. Pull SUS_S3# (PC15) to GND through ~1 kOhm
2. Hold for > 500 ms (SUS_S3_THRESHOLD_MS)
3. DMM or LA on PWRBTN# (PC14): LOW pulse ~150 ms (PWRBTN_PULSE_MS)
4. Release SUS_S3#; do not repeat sooner than 5 s (SUS_S3_COOLDOWN_MS)

MCU does not expose SUS_S3#/PWRBTN# in GET_STATUS — not automatable over UART.

[SKIP] SUS_S3/PWRBTN: manual steps only (set RUN_SUS_S3_HW_TEST=1 to count as PASS in suite)

=== Result: 8 PASS, 1 SKIP, 7 FAIL ===
Unavailable without Q7: BOOTLOADER_ENTER OTA, Linux fault recovery, PGOOD+Q7 power cycle
Optional bare-board suite: make test-uart (Tests_UART/)
make: \*\*\* [Makefile:441: test-uart-all] Error 1
