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
