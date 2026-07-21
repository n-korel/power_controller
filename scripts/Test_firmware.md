stty -F /dev/ttyUSB0 115200 cs8 -cstopb -parenb raw -echo min 0 time 10

exec 3<>/dev/ttyUSB0

printf '\x02\x01\x00\x15\x03' >&3

dd bs=1 count=64 <&3 status=none 2>/dev/null | xxd

exec 3<&-
exec 3>&-

make bl-ping
make bl-set BL_PERCENT=75
make bl-set-pwm BL_PWM=800
make bl-off
make bl-on-display

scp build/POWER_Controller_BNT.bin root@q7:/opt/BNT_STM32/

export PATH="/opt/stm32flash:$PATH"
cd /opt/BNT_STM32/BNT_TESTS
FW_BIN=/opt/BNT_STM32/POWER_Controller_BNT.bin ./34_ota_confirm_reset_fault.sh

md5sum build/POWER_Controller_BNT.bin
