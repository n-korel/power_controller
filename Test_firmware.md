# Подробные сценарии проверки POWER_Controller — без Q7

**Оборудование:** питание платы, USB-UART TTL 3.3В (PA9/PA10), мультиметр, опционально логический анализатор.  
**Порт:** `/dev/ttyUSB0` (или ACM0 — смотреть `dmesg | tail` после подключения адаптера).  
**Параметры порта:** 115200, 8N1, без flow control.

Все кадры отправляются в hex. CRC считается по `[CMD][LEN][DATA]`, poly 0x07, init 0x00. Готовые скрипты — в `Tests_UART/`.

---

## Раздел 1. Питание и старт

### П.1 — Напряжения до прошивки

**Цель:** убедиться что питание платы в норме прежде чем прошивать и смотреть UART.

**Шаги:**

1. Подключить мультиметр, чёрный щуп на GND платы.
2. Красный щуп на `+3.3V_A` (VDD MCU по схеме).
3. Записать показание.
4. Красный щуп на `VDDA` (вывод VDDA MCU, питается от IC9 RS3112-2.5XSF3).
5. Записать показание.

**Ожидание:**

- `+3.3V_A` = 3.25–3.35 В
- `VDDA` = 2.45–2.55 В

**Если не так:**

- `+3.3V_A` отсутствует → нет питания MCU, смотреть входное питание и LDO.
- `VDDA` = 3.3 В вместо 2.5 В → IC9 не работает или перепутаны цепи. Прошивка будет считать токи неправильно.
- `VDDA` = 0 → нет аналогового питания, ADC не будет работать.

---

### П.2 — Нормальный старт при PGOOD=HIGH

**Цель:** убедиться что MCU стартует, выполняет auto-startup и отвечает по UART.

**Шаги:**

1. Убедиться что PGOOD высокий в норме (по схеме R205 подтягивает к VMCU — должно быть 3.3В без внешнего воздействия).
2. Подключить USB-UART: PA9 (TX MCU) → RX адаптера, PA10 (RX MCU) ← TX адаптера, GND общий.
3. Открыть терминал: `stty -F /dev/ttyUSB0 115200 cs8 -cstopb -parenb raw`
4. Подать питание платы.
5. Подождать 200–500 мс.
6. Отправить PING: `echo -ne '\x02\x01\x00\x15\x03' > /dev/ttyUSB0`
7. Читать ответ (6 байт): `xxd /dev/ttyUSB0`

**Ожидание:**

- Ответ на PING: `02 01 01 AA 21 03`
  - `02` = STX
  - `01` = CMD (PING)
  - `01` = LEN (1 байт данных)
  - `AA` = статус «MCU alive»
  - `21` = CRC
  - `03` = ETX

**Затем GET_STATUS:**

- Отправить: `02 04 00 54 03`
- Ответ: 31 байт. Разобрать bytes 23 (offset 22 в DATA) = `state`, bytes 24-25 = `fault_flags`.
- `state` должен быть `0x07` = SCALER+LCD+BACKLIGHT (display sequencing §6.1 при `ENABLE_BACKLIGHT_AUTO_STARTUP=1`); AUDIO/TOUCH выключены (`ENABLE_AUDIO_HW`/`ENABLE_TOUCH_HW` = 0). Без auto-BL — `0x03`.
- `fault_flags` = `0x00 0x00` — нет аварий.

**Если не так:**

- Нет ответа на PING → MCU не стартовал или TX/RX перепутаны. Попробовать поменять RX/TX на адаптере.
- `state = 0x00` при `fault_flags != 0` → был fault при старте, смотреть П.3/раздел Fault.
- `state = 0x00` и `fault_flags = 0` → MCU ждёт PGOOD или не завершил секвенс. Подождать ещё 5–6 секунд.

---

### П.3 — Старт при PGOOD=LOW

**Цель:** убедиться что MCU правильно обнаруживает отсутствие питания и защёлкивает fault.

**Шаги:**

1. Найти на плате сигнал PGOOD (PA8 MCU). По схеме R205 = 10 кОм подтяжка к VMCU.
2. Безопасно притянуть PGOOD к GND (перемычка к GND через 1 кОм чтобы не замкнуть напрямую).
3. Подать питание платы.
4. Подождать 6 секунд (PGOOD_TIMEOUT_MS = 5000 мс, дать запас).
5. Отправить GET_STATUS: `02 04 00 54 03`
6. Разобрать ответ.

**Ожидание:**

- `fault_flags` = `0x00 0x80` (little-endian) → `0x0080` → бит 7 = `FAULT_PGOOD_LOST`.
- `state` = `0x00` — все домены выключены.
- `inputs` byte (offset 25 в DATA): бит 6 = 0 (PGOOD = LOW).

**Как посчитать вручную:**

- DATA = байты с offset 3 по 28 в ответе (нумерация с 0).
- `fault_flags` = DATA[23] + DATA[24]\*256. При `DATA[23]=0x80, DATA[24]=0x00` → `fault = 0x0080`.

---

### П.4 — Восстановление PGOOD без сброса MCU

**Цель:** убедиться что домены не включаются сами при снятии fault-условия.

**Шаги:**

1. Продолжение П.3: PGOOD=LOW, fault защёлкнут.
2. Убрать перемычку GND с PGOOD (вернуть HIGH).
3. Подождать 1 секунду.
4. GET_STATUS.

**Ожидание:**

- `fault_flags` всё ещё `0x0080` — fault latched, не сбросился сам.
- `state = 0x00` — домены не включились.

**Шаги для восстановления (проверка сценария):** 5. Отправить RESET_FAULT: `02 05 00 BF 03` 6. GET_STATUS.

**Ожидание после RESET_FAULT:**

- `fault_flags = 0x0000` — флаги сброшены.
- `state = 0x00` — домены по-прежнему выключены. RESET_FAULT не включает нагрузки.

---

## Раздел 2. Протокол UART

> Все скрипты запускать из корня репозитория: `bash Tests_UART/NN_name.sh`  
> Или одной командой: `make test-uart`

### У.1 — PING

```bash
bash Tests_UART/01_ping.sh
```

**Что происходит:** скрипт отправляет `02 01 00 15 03`, читает 6 байт, проверяет DATA[0]=0xAA.

**Лог успеха:**

```
[INFO] PING on /dev/ttyUSB0
00000000: 0201 01aa 2103                            ....!.
[PASS] PING: status=0xAA
```

---

### У.2 — GET_STATUS: структура 31 байт

```bash
bash Tests_UART/03_reset_fault.sh   # сначала сбросить fault если есть
bash Tests_UART/02_get_status.sh
```

**Что проверяет скрипт:** длина ответа = 31 байт, CMD=0x04, LEN=0x1A, ETX=0x03.

**Ручная расшифровка ответа** (если хотите понять структуру):

```
Байт 0:    02       STX
Байт 1:    04       CMD = GET_STATUS
Байт 2:    1A       LEN = 26 (байт данных)
Байт 3-4:  XX XX    v24 (мВ, little-endian)
Байт 5-6:  XX XX    v12 (мВ)
Байт 7-8:  XX XX    v5  (мВ)
Байт 9-10: XX XX    v3v3 (мВ)
Байт 11-12:XX XX    i_lcd (мА)
Байт 13-14:XX XX    i_backlight (мА)
Байт 15-16:XX XX    i_scaler (мА)
Байт 17-18:XX XX    i_audio_l (мА)
Байт 19-20:XX XX    i_audio_r (мА)
Байт 21-22:XX XX    temp0 (int16, -32768 если нет NTC)
Байт 23-24:XX XX    temp1 (int16, -32768 если нет NTC)
Байт 25:   XX       state (битовая маска доменов)
Байт 26-27:XX XX    fault_flags (little-endian)
Байт 28:   XX       inputs
Байт 29:   XX       CRC
Байт 30:   03       ETX
```

**Пример реального ответа** (плата с 12В и 5В, без 24В):

```
02 04 1A
00 00        v24  = 0 мВ (нет 24В на стенде)
D0 2E        v12  = 0x2ED0 = 11984 мВ ≈ 12.0 В
74 13        v5   = 0x1374 = 4980 мВ ≈ 5.0 В
E4 0C        v3v3 = 0x0CE4 = 3300 мВ = 3.3 В
...          токи (около нуля без нагрузки)
00 80        temp0 = -32768 (NTC нет)
00 80        temp1 = -32768 (NTC нет)
07           state = 0x07 (SCALER|LCD|BACKLIGHT ON, AUDIO/TOUCH OFF)
00 00        fault_flags = 0
4F           inputs = 0x4F = 01001111 (PGOOD=1, IN_0..3=1)
XX 03
```

---

### У.3 — Неверный CRC

```bash
bash Tests_UART/07_bad_crc.sh
```

**Что происходит:** скрипт отправляет GET_STATUS с CRC=0xFF, ждёт 250 мс, ожидает тишину. Затем PING.

**Ожидание:** никакого ответа на плохой кадр, PING после работает нормально.

**Ручная проверка:**

```bash
# Отправить кадр с битым CRC
echo -ne '\x02\x04\x00\xff\x03' > /dev/ttyUSB0
# Подождать 300 мс — ответа быть не должно
sleep 0.3
# Затем PING — должен работать
echo -ne '\x02\x01\x00\x15\x03' > /dev/ttyUSB0
```

---

### У.4 — Неизвестная команда

```bash
bash Tests_UART/08_unknown_cmd.sh
```

**Что проверяет:** команда 0xFF → NACK (`CMD=0xFF, DATA=0x01`) или тишина. Следующий PING работает.

---

### У.5 — Пакетный таймаут (>50 мс)

```bash
bash Tests_UART/10_packet_timeout.sh
```

**Что происходит:** отправляет `02 04 00` (начало GET_STATUS без CRC/ETX), ждёт 60 мс (> `UART_PACKET_TIMEOUT_MS=50`), затем PING.

**Ожидание:** незавершённый пакет отброшен, PING получает корректный ответ.

---

### У.6 — Межбайтовый таймаут (>10 мс)

```bash
bash Tests_UART/11_interbyte_gap.sh
```

**Что происходит:** байты PING отправляются по одному с паузой 15 мс (> `UART_INTERBYTE_TIMEOUT_MS=10`). Ожидается тишина, затем полный PING работает.

---

### У.7 — Ресинхронизация по STX

```bash
bash Tests_UART/09_resync_stx.sh
```

**Что проверяет:** в середине «мусорного» потока встречается новый STX (0x02) — MCU сбрасывает текущий парсер и начинает новый кадр.

---

### У.8 — Стресс 20× GET_STATUS

```bash
bash Tests_UART/12_stress_get_status.sh
```

**Что проверяет:** 20 запросов GET_STATUS с паузой 50 мс. Все должны вернуть 31 байт. Проверяет отсутствие зависаний и переполнения очереди.

---

### У.9 — Полный прогон

```bash
make test-uart
# или
bash Tests_UART/run_all_bare_board.sh
```

Включает также **K.3, K.5, A.2/A.3, F.1, F.3, I.1** (`14`–`19`) и негативные протокол/fault **`20`–`21`, `23`, `24`–`25`, `26`**.

**Ожидаемый вывод:**

```
=== POWER_Controller UART — bare board ===
Port: /dev/ttyUSB0

--- 00_flush_port.sh ---
[PASS] Port /dev/ttyUSB0 ready (115200 8N1 raw)

--- 01_ping.sh ---
[PASS] PING: status=0xAA

--- 03_reset_fault.sh ---
[PASS] RESET_FAULT ACK OK
[PASS] fault_flags=0, state=0 (loads did not turn on)
...
--- 19_iwdg_stress.sh ---
[PASS] I.1: 20/20 GET_STATUS without MCU reset
=== Result: 25 PASS, 0 FAIL ===
```

---

## Раздел 3. Политика POWER_CTRL

### К.1 — BACKLIGHT без SCALER/LCD → отказ

```bash
bash Tests_UART/04_neg_backlight.sh
```

**Вручную:**

```bash
# Убедиться что SCALER/LCD выключены (после reset state=0x00)
# Попытаться включить только BACKLIGHT (бит 2)
# Кадр: CMD=0x02, LEN=4, DATA=mask_lo mask_hi val_lo val_hi
# mask=0x0004, value=0x0004 → 04 00 04 00
# CRC считается по: 02 04 04 00 04 00 → CRC = D8
echo -ne '\x02\x02\x04\x04\x00\x04\x00\xD8\x03' > /dev/ttyUSB0
```

**Ожидание:**

- Ответ: `02 02 01 01 [CRC] 03`
- DATA[0] = `0x01` = статус «отказ»
- GET_STATUS после: state не изменился.

---

### К.2 — LCD без SCALER → отказ

```bash
bash Tests_UART/05_neg_lcd_no_scaler.sh
```

**Вручную:**

```bash
# mask=0x0002, value=0x0002 (LCD ON, SCALER OFF)
# CRC по: 02 04 02 00 02 00 → CRC = E3
echo -ne '\x02\x02\x04\x02\x00\x02\x00\xE3\x03' > /dev/ttyUSB0
```

**Ожидание:** status=0x01, state не изменился.

---

### К.3 — Атомарность: SCALER+BACKLIGHT без LCD

```bash
bash Tests_UART/14_neg_scaler_backlight.sh
```

**Цель:** убедиться что команда, нарушающая политику, отклоняется целиком — ни SCALER, ни BACKLIGHT не включаются.

**Вручную:**

```bash
# mask=0x0005 (SCALER|BACKLIGHT), value=0x0005, LCD=0
# Кадр: 02 04 05 00 05 00 [CRC] 03
# CRC по: 02 04 05 00 05 00 → CRC = ...
python3 -c "
data=bytes([0x02,0x04,0x05,0x00,0x05,0x00])
crc=0
for b in data[1:]: crc = [0x00,0x07,0x0E,0x09,0x1C,0x1B,0x12,0x15,0x38,0x3F,0x36,0x31,0x24,0x23,0x2A,0x2D,0x70,0x77,0x7E,0x79,0x6C,0x6B,0x62,0x65,0x48,0x4F,0x46,0x41,0x54,0x53,0x5A,0x5D,0xE0,0xE7,0xEE,0xE9,0xFC,0xFB,0xF2,0xF5,0xD8,0xDF,0xD6,0xD1,0xC4,0xC3,0xCA,0xCD,0x90,0x97,0x9E,0x99,0x8C,0x8B,0x82,0x85,0xA8,0xAF,0xA6,0xA1,0xB4,0xB3,0xBA,0xBD,0xC7,0xC0,0xC9,0xCE,0xDB,0xDC,0xD5,0xD2,0xFF,0xF8,0xF1,0xF6,0xE3,0xE4,0xED,0xEA,0xB7,0xB0,0xB9,0xBE,0xAB,0xAC,0xA5,0xA2,0x8F,0x88,0x81,0x86,0x93,0x94,0x9D,0x9A,0x27,0x20,0x29,0x2E,0x3B,0x3C,0x35,0x32,0x1F,0x18,0x11,0x16,0x03,0x04,0x0D,0x0A,0x57,0x50,0x59,0x5E,0x4B,0x4C,0x45,0x42,0x6F,0x68,0x61,0x66,0x73,0x74,0x7D,0x7A,0x89,0x8E,0x87,0x80,0x95,0x92,0x9B,0x9C,0xB1,0xB6,0xBF,0xB8,0xAD,0xAA,0xA3,0xA4,0xF9,0xFE,0xF7,0xF0,0xE5,0xE2,0xEB,0xEC,0xC1,0xC6,0xCF,0xC8,0xDD,0xDA,0xD3,0xD4,0x69,0x6E,0x67,0x60,0x75,0x72,0x7B,0x7C,0x51,0x56,0x5F,0x58,0x4D,0x4A,0x43,0x44,0x19,0x1E,0x17,0x10,0x05,0x02,0x0B,0x0C,0x21,0x26,0x2F,0x28,0x3D,0x3A,0x33,0x34,0x4E,0x49,0x40,0x47,0x52,0x55,0x5C,0x5B,0x76,0x71,0x78,0x7F,0x6A,0x6D,0x64,0x63,0x3E,0x39,0x30,0x37,0x22,0x25,0x2C,0x2B,0x06,0x01,0x08,0x0F,0x1A,0x1D,0x14,0x13,0xAE,0xA9,0xA0,0xA7,0xB2,0xB5,0xBC,0xBB,0x96,0x91,0x98,0x9F,0x8A,0x8D,0x84,0x83,0xDE,0xD9,0xD0,0xD7,0xC2,0xC5,0xCC,0xCB,0xE6,0xE1,0xE8,0xEF,0xFA,0xFD,0xF4,0xF3][crc ^ b]
print(hex(crc))
"
```

Проще использовать `lib.sh` из Tests_UART:

```bash
source Tests_UART/lib.sh
uart_open
hex=$(cmd_power_ctrl 0x0005 0x0005)
echo "$hex" | xxd -r -p | xxd
# Ожидание: байт 3 = 01 (status=rejected)
after=$(cmd_get_status)
parse_get_status_hex "$after"
# state должен остаться прежним
```

---

### К.4 — SCALER+LCD без дисплея → SEQ_ABORT (ожидаемо)

```bash
bash Tests_UART/13_optional_power_ctrl_seq_fault.sh
```

**Что происходит:** команда `POWER_CTRL SCALER|LCD ON` принимается (`status=0x00`), MCU начинает power sequencing. Через ~200 мс (SEQ_VERIFY_TIMEOUT) не видит напряжения SCALER_POWER_M, защёлкивает `FAULT_SEQ_ABORT | FAULT_SCALER`.

**Ожидание:**

- ACK на POWER_CTRL: status=0x00 (команда принята, не отклонена)
- GET_STATUS после: `fault_flags = 0x2001`
  - `0x2001` = `0x2000 | 0x0001` = `FAULT_SEQ_ABORT | FAULT_SCALER`
  - `state = 0x00` (все домены выключены)

**После теста — обязательно сбросить fault:**

```bash
bash Tests_UART/03_reset_fault.sh
```

---

### К.5 — ETH1, ETH2, TOUCH

```bash
bash Tests_UART/15_simple_domains.sh
```

**Цель:** убедиться что простые домены переключаются без секвенса.

```bash
source Tests_UART/lib.sh
uart_open

# Включить TOUCH (бит 6 = 0x40)
hex=$(cmd_power_ctrl 0x0040 0x0040)
parse_get_status_hex "$(cmd_get_status)" | grep state
# state должен содержать бит 6: 0x40 или OR с текущим

# Включить ETH1 (бит 4 = 0x10) и ETH2 (бит 5 = 0x20)
hex=$(cmd_power_ctrl 0x0030 0x0030)
parse_get_status_hex "$(cmd_get_status)" | grep state
# state: биты 4,5 установлены

# Выключить ETH1
hex=$(cmd_power_ctrl 0x0010 0x0000)
parse_get_status_hex "$(cmd_get_status)" | grep state
# бит 4 = 0
```

**Опционально с мультиметром:**

- PB7 = POWER_ETH1: при включении ≈3.3В, при выключении ≈0В
- PB6 = POWER_ETH2: аналогично
- PB2 = POWER_TOUCH: аналогично

---

## Раздел 4. Телеметрия АЦП

### А.1 — Напряжения vs мультиметр

**Шаги:**

1. GET_STATUS, записать поля v12, v5, v3v3 (v24=0 если нет 24В на стенде).
2. Мультиметром измерить реальные шины.
3. Сравнить.

**Расшифровка значений из GET_STATUS:**

```bash
source Tests_UART/lib.sh
uart_open
parse_get_status_hex "$(cmd_get_status)"
# Вывод:
# v24=0
# v12=11984     ← мВ
# v5=4980
# v3v3=3301
# ...
```

**Допустимое расхождение:** ±10% (делитель 4.99кОм/470 Ом, VDDA=2.5В).

**Формула пересчёта для понимания:**

- ADC_raw → ADC_mV: `mv = raw * 2500 / 4096`
- ADC_mV → реальное напряжение: `v_real = adc_mv * (4990 + 470) / 470 = adc_mv * 11.617`

---

### А.2 — Токи при нулевой нагрузке

```bash
bash Tests_UART/16_telemetry_sanity.sh
```

**Шаги:**

1. Убедиться что нагрузки выключены (state=0x00 или только TOUCH/ETH).
2. GET_STATUS, смотреть i_lcd, i_backlight, i_scaler, i_audio_l, i_audio_r.

```bash
source Tests_UART/lib.sh
uart_open
parse_get_status_hex "$(cmd_get_status)"
# i_lcd=...
# i_backlight=...
# i_scaler=...
# i_audio_l=...
# i_audio_r=...
```

**Ожидание:** значения в диапазоне от −200 до +200 мА. Небольшое ненулевое значение — норма (дрейф offset датчика).

**Если значения большие (>500 мА) при нулевой нагрузке:**

- Датчик не откалиброван. Выполнить CALIBRATE_OFFSET (`02 09 00 89 03`) при `state=0`. Скрипт `16_telemetry_sanity.sh` вызывает это автоматически.

---

### А.3 — Температурные каналы

```bash
source Tests_UART/lib.sh
uart_open
parse_get_status_hex "$(cmd_get_status)" | grep temp
# temp0=-32768
# temp1=-32768
```

**Ожидание:** оба = −32768. NTC не установлены на текущей ревизии. Любое другое значение — повод проверить схему.

---

### А.4 — Поле inputs: PGOOD и IN_0..IN_5

**Расшифровка байта inputs:**

```
Бит 0 = IN_0 (PB15)
Бит 1 = IN_1 (PB14)
Бит 2 = IN_2 (PB13)
Бит 3 = IN_3 (PB12)
Бит 4 = IN_4 (PB11)
Бит 5 = IN_5 (PB10)
Бит 6 = PGOOD (PA8) — 1 = HIGH (питание в норме)
Бит 7 = Faultz (PC7) — 1 = HIGH (нет fault усилителя; active LOW на железе)
```

**Шаги:**

1. Нормальное состояние: GET_STATUS, записать inputs.
2. Перемычкой соединить IN_0 (PB15) с GND. Подождать 25 мс (дебаунс 20 мс). GET_STATUS.
3. Проверить что бит 0 в inputs изменился с 1 на 0.
4. Убрать перемычку, подождать 25 мс, GET_STATUS — бит 0 вернулся в 1.

```bash
source Tests_UART/lib.sh
uart_open

echo "=== Исходное состояние ==="
parse_get_status_hex "$(cmd_get_status)" | grep inputs

echo "=== Замкнуть IN_0 на GND, подождать 30 мс, затем Enter ==="
read
sleep 0.05
parse_get_status_hex "$(cmd_get_status)" | grep inputs
# inputs должен изменить бит 0
```

**Пример:**

- До: `inputs=0x4F` = `01001111` → PGOOD=1, IN_0..3=1
- После замыкания IN_0 на GND: `inputs=0x4E` = `01001110` → IN_0=0

---

## Раздел 5. Fault и восстановление

### Ф.1 — Принудительный fault через SET_THRESHOLDS

```bash
bash Tests_UART/17_fault_v12_range.sh
```

**Цель:** убедиться что система правильно обнаруживает выход параметра за пределы и защёлкивает fault.

**Метод:** сузить пороги V12 так, чтобы реальное напряжение оказалось вне диапазона.

**Шаги:**

1. GET_STATUS, записать реальное v12 (например, 11984 мВ).
2. Отправить SET_THRESHOLDS: установить V12_MIN=13000, V12_MAX=14000 (заведомо выше реального).

**Формат SET_THRESHOLDS (CMD=0x07):**

```
mask = 0x0002 (бит 1 = V12_MIN/V12_MAX)
данные: mask_lo mask_hi v12_min_lo v12_min_hi v12_max_lo v12_max_hi
= 02 00  20 4E  10 36
(0x4E20=20000 мВ min, 0x3610=13840 мВ max — подставьте нужные)
```

Проще всего через lib.sh:

```bash
source Tests_UART/lib.sh
uart_open

# Текущее V12
parse_get_status_hex "$(cmd_get_status)" | grep v12

# Установить V12: min=13000 (0x32C8), max=14000 (0x36B0)
# mask=0x0002, данные: 02 00 C8 32 B0 36
frame=$(python3 -c "
import sys
data = [0x02, 0x00, 0xC8, 0x32, 0xB0, 0x36]
tbl=[0x00,0x07,0x0E,0x09,0x1C,0x1B,0x12,0x15,0x38,0x3F,0x36,0x31,0x24,0x23,0x2A,0x2D,0x70,0x77,0x7E,0x79,0x6C,0x6B,0x62,0x65,0x48,0x4F,0x46,0x41,0x54,0x53,0x5A,0x5D,0xE0,0xE7,0xEE,0xE9,0xFC,0xFB,0xF2,0xF5,0xD8,0xDF,0xD6,0xD1,0xC4,0xC3,0xCA,0xCD,0x90,0x97,0x9E,0x99,0x8C,0x8B,0x82,0x85,0xA8,0xAF,0xA6,0xA1,0xB4,0xB3,0xBA,0xBD,0xC7,0xC0,0xC9,0xCE,0xDB,0xDC,0xD5,0xD2,0xFF,0xF8,0xF1,0xF6,0xE3,0xE4,0xED,0xEA,0xB7,0xB0,0xB9,0xBE,0xAB,0xAC,0xA5,0xA2,0x8F,0x88,0x81,0x86,0x93,0x94,0x9D,0x9A,0x27,0x20,0x29,0x2E,0x3B,0x3C,0x35,0x32,0x1F,0x18,0x11,0x16,0x03,0x04,0x0D,0x0A,0x57,0x50,0x59,0x5E,0x4B,0x4C,0x45,0x42,0x6F,0x68,0x61,0x66,0x73,0x74,0x7D,0x7A,0x89,0x8E,0x87,0x80,0x95,0x92,0x9B,0x9C,0xB1,0xB6,0xBF,0xB8,0xAD,0xAA,0xA3,0xA4,0xF9,0xFE,0xF7,0xF0,0xE5,0xE2,0xEB,0xEC,0xC1,0xC6,0xCF,0xC8,0xDD,0xDA,0xD3,0xD4,0x69,0x6E,0x67,0x60,0x75,0x72,0x7B,0x7C,0x51,0x56,0x5F,0x58,0x4D,0x4A,0x43,0x44,0x19,0x1E,0x17,0x10,0x05,0x02,0x0B,0x0C,0x21,0x26,0x2F,0x28,0x3D,0x3A,0x33,0x34,0x4E,0x49,0x40,0x47,0x52,0x55,0x5C,0x5B,0x76,0x71,0x78,0x7F,0x6A,0x6D,0x64,0x63,0x3E,0x39,0x30,0x37,0x22,0x25,0x2C,0x2B,0x06,0x01,0x08,0x0F,0x1A,0x1D,0x14,0x13,0xAE,0xA9,0xA0,0xA7,0xB2,0xB5,0xBC,0xBB,0x96,0x91,0x98,0x9F,0x8A,0x8D,0x84,0x83,0xDE,0xD9,0xD0,0xD7,0xC2,0xC5,0xCC,0xCB,0xE6,0xE1,0xE8,0xEF,0xFA,0xFD,0xF4,0xF3]
body = bytes([0x07, len(data)] + data)
crc = 0
for b in body: crc = tbl[crc ^ b]
frame = bytes([0x02]) + body + bytes([crc, 0x03])
print(frame.hex())
")
echo -ne \"\$(echo $frame | xxd -r -p)\" > /dev/ttyUSB0
sleep 0.2
# Читать ACK: status=0x00
```

3. Включить любой домен (например TOUCH): иначе `fault_manager` **не** проверяет шины при `state=0` — см. `fault_manager.c`. Скрипт `17_fault_v12_range.sh` делает это сам.
4. Подождать latch (~0.5–3 с, 5 подтверждений ADC).
5. GET_STATUS.

**Ожидание:**

- `fault_flags = 0x0400` → бит 10 = `FAULT_V12_RANGE`
- `state = 0x00`

5. Восстановить пороги обратно:

```bash
# mask=0x0002, V12_MIN=10000 (0x2710), V12_MAX=13000 (0x32C8)
# Это норм значения из config.h
```

---

### Ф.2 — Safe state при fault

**После любого fault (например Ф.1):**

```bash
source Tests_UART/lib.sh
uart_open
parse_get_status_hex "$(cmd_get_status)"
```

**Ожидание:**

- `state=0x00` — все домены выключены
- `fault_flags != 0`

**Дополнительно (с мультиметром):**

- PB5 (SCALER_POWER_ON) = 0В
- PB4 (LCD_POWER_ON) = 0В
- PA15 (BACKLIGHT_ON) = 0В
- PB9 (BL_PWM) = 0В (нет PWM)

---

### Ф.3 — FAULT_RESERVED всегда 0

```bash
bash Tests_UART/18_fault_reserved.sh
```

```bash
source Tests_UART/lib.sh
uart_open

# Спровоцировать fault (например П.3 или Ф.1)
# Затем проверить бит 15:
python3 -c "
import sys
hex_response = '$(cmd_get_status)'
raw = bytes.fromhex(hex_response)
fault = raw[26] | (raw[27] << 8)
print(f'fault_flags = 0x{fault:04X}')
print(f'FAULT_RESERVED (bit15) = {(fault >> 15) & 1}')
assert (fault & 0x8000) == 0, 'ОШИБКА: бит 15 установлен!'
print('OK: бит 15 = 0')
"
```

---

### Ф.4 — RESET_FAULT не включает нагрузки

**Шаги:**

1. Убедиться что есть активный fault (state=0x00, fault_flags!=0).
2. Отправить RESET_FAULT.
3. GET_STATUS.

```bash
source Tests_UART/lib.sh
uart_open

echo "=== До RESET_FAULT ==="
parse_get_status_hex "$(cmd_get_status)" | grep -E 'state|fault'

hex=$(cmd_reset_fault)
echo "$hex" | xxd -r -p | xxd
# status должен быть 0x00

echo "=== После RESET_FAULT ==="
parse_get_status_hex "$(cmd_get_status)" | grep -E 'state|fault'
# fault_flags=0x0000, state=0x00
```

**Ожидание:**

- `fault_flags = 0x0000` — флаги сброшены
- `state = 0x00` — домены не включились автоматически

---

## Раздел 6. RESET_BRIDGE

### Р.1 — Импульс на PB8

```bash
bash Tests_UART/06_reset_bridge.sh
```

**Ожидание:** ACK status=0x00.

**С логическим анализатором (опционально):**

- Щуп LA на PB8 (RST_CH7511B, open-drain)
- В нормальном состоянии: HIGH (подтяжка на плате)
- После команды: импульс LOW длительностью ~10 мс, затем возврат в HIGH
- `BRIDGE_RST_PULSE_MS = 10`

**Без LA:** достаточно ACK — функция отправки импульса простая, не зависит от состояния дисплея.

---

## Раздел 7. IWDG

### И.1 — Нет ложных reset при нормальной работе

```bash
bash Tests_UART/19_iwdg_stress.sh
```

**Цель:** убедиться что MCU не перезагружается от IWDG в нормальном режиме.

**IWDG параметры (из iwdg.c):**

- Prescaler = 256, Reload = 4095, Window = 4095
- Timeout = 4095 / (LSI/256) = 4095 / (40000/256) ≈ 26.2 с

MCU должен делать `HAL_IWDG_Refresh` в main loop быстрее чем за ~26 с.

**Шаги:**

```bash
source Tests_UART/lib.sh
uart_open

echo "Отправляем 20 GET_STATUS с интервалом 100 мс..."
for i in $(seq 1 20); do
  hex=$(cmd_get_status) || { echo "FAIL: нет ответа на итерации $i"; exit 1; }
  echo -n "."
done
echo ""
echo "PASS: все 20 ответов получены, нет reset"
```

**Если MCU перезагружается:** после reset PING не отвечает ~100 мс. Если в промежутке между двумя GET_STATUS пропадает ответ и потом восстанавливается — это reset от IWDG. Не должно происходить.

---

## Раздел 8. Bootloader

### Б.1 — BOOTLOADER_ENTER → ROM bootloader

**Цель:** убедиться что MCU переходит в ROM UART bootloader и принимает подключение от stm32flash.

**Требования:** установленный `stm32flash` (`sudo apt install stm32flash` или собрать из исходников).

**Шаги:**

```bash
source Tests_UART/lib.sh
uart_open

# Отправить BOOTLOADER_ENTER (CMD=0x08)
hex=$(cmd_ping)  # сначала проверить связь

# Вручную отправить 0x08
uart_tx_frame 0x08
sleep 0.2
# Должен прийти ACK: 02 08 01 00 [CRC] 03
ack=$(uart_rx 6 0.5) || echo "нет ACK"
echo "ACK: $ack"
echo "$ack" | xxd -r -p | xxd

# Закрыть порт прежде чем stm32flash откроет его
uart_close

sleep 0.5  # дать MCU время выполнить reset и войти в bootloader

# Проверить что ROM bootloader отвечает
stm32flash -b 115200 /dev/ttyUSB0
```

**Ожидание от stm32flash:**

```
stm32flash 0.7

http://stm32flash.sourceforge.net/

Interface serial_posix: 115200 8E1
Version      : 0x31
Option 1     : 0x00
Option 2     : 0x00
Device ID    : 0x0444 (STM32F03xx)
- RAM        : Up to 8KiB  (2048b reserved by bootloader)
- Flash      : Up to 64KiB (size first sector: 4x1024)
...
```

Если видите информацию о чипе — ROM bootloader работает.

**После проверки** — перезагрузить MCU (кнопка NRST или снять/подать питание). Прошивка снова запустится из Flash (BOOT0=LOW).

### Б.2 — OTA budget vs IWDG timeout

**Цель:** убедиться, что watchdog-окно покрывает полный OTA через ROM bootloader.

**Исходные значения (текущая конфигурация):**

- `LSI = 40000 Гц`
- `Prescaler = 256`
- `Reload = 4095`
- `IWDG timeout = Reload / (LSI/Prescaler) = 4095 / (40000/256) ≈ 26.2 с`

**Практический бюджет для 64 КБ по UART 115200:**

- типично `10..15 с` на erase/write/verify (`stm32flash`)
- рекомендуемый запас `>= 30%`
- допустимый максимум для бюджетной оценки: `15 * 1.3 = 19.5 с`

**Критерий PASS:** `IWDG timeout >= 20.0 с`.

**Автопроверка в репозитории:**

```bash
python3 Tests/contract_check.py
```

Скрипт валит проверку, если:

- `.ioc` и `Core/Src/iwdg.c` рассинхронизированы по `Prescaler/Reload/Window`
- расчётный `IWDG timeout` меньше `20.0 с`

---

## Полный порядок прогона на голой плате

```bash
# 1. Проверить питание мультиметром (П.1) — без прошивки

# 2. Прошить MCU через SWD
# openocd или STM32CubeProgrammer

# 3. Подключить USB-UART, убедиться в правильном порту
dmesg | tail -5

# 4. Полный прогон скриптов
make test-uart

# 5. Опциональный тест SEQ_ABORT без дисплея
bash Tests_UART/13_optional_power_ctrl_seq_fault.sh
bash Tests_UART/03_reset_fault.sh  # сбросить после

# 6. Ручная проверка телеметрии
source Tests_UART/lib.sh
uart_open
parse_get_status_hex "$(cmd_get_status)"
# Сравнить v12/v5/v3v3 с мультиметром

# 7. Проверка fault через SET_THRESHOLDS (Ф.1)
# Затем RESET_FAULT (Ф.4)

# 8. Bootloader (Б.1)
# stm32flash -b 115200 /dev/ttyUSB0
```

---

## Диагностика BACKLIGHT: BOR vs PGOOD (bench)

Подтверждено осциллографом: при `BACKLIGHT_ON` → HIGH просадка **3.3V_A (VMCU)** с ~3.3 V до **~2.8 V** → срабатывание BOR, полный reset MCU.

### Два сценария сбоя при включении BL

| Сценарий | Что происходит | Grace period (`SEQ_BL_PGOOD_GRACE_MS`) |
|----------|----------------|----------------------------------------|
| **A. BOR** | VMCU ниже порога BOR (~2.8 V), MCU сбрасывается до обработки fault | **Не помогает** — reset уже произошёл |
| **B. PGOOD-глитч** | PGOOD просаживается >20 ms, `fault_set_flag(PGOOD\|SEQ_ABORT)`, MCU работает | **Может помочь** — блокирует fault в окне inrush |

### GET_STATUS: что смотреть после `POWER_CTRL BACKLIGHT ON`

Поля в кадре GET_STATUS (см. `contract/protocol.yaml`, offset 25–32):

| Поле | BOR (сценарий A) | PGOOD/fault (сценарий B) | Успех |
|------|------------------|---------------------------|-------|
| `state` | `0x00` | `0x00` или частично ON | `0x07` |
| `fault_flags` | `0x0000` | `0x0080` / `0x2080` типично | `0x0000` |
| `last_power_ctrl_mask_lo` | **`0x00`** | **`0x04`** (запрос сохранился) | `0x04` или `0x07` |
| `dseq` | `0` | может быть ≠0 до fault | `0` после завершения |
| `pgood` | `1` *сейчас* | может быть `1` после глитча | `1` |

**Главный критерий reset во время BL:** хост отправил BL (`mask_lo` содержит `0x04`), а в ответе **`last_power_ctrl_mask_lo=0x00`**.  
Поле обнуляется только в `power_manager_init()` после hardware reset. `power_safe_state()` его **не** трогает.

### Почему `reset_flags_raw=0x0c800003` не однозначен

Биты 31–24 = `0x0C`: `PORRSTF` (bit 27) + `PINRSTF` (bit 26).

- `PORRSTF=1` — и при **холодном включении**, и при **BOR** (один флаг POR/PDR в RCC).
- Значение **одинаково** после каждого boot → не отличает «первое включение» от «BOR при BL».
- `boot_counter` в `.noinit` при глубокой просадке SRAM **ненадёжен**.

Для диагностики опираться на **`last_power_ctrl_mask_lo`**, не на `reset_flags_raw`.

### BOR-маркер в `last_power_ctrl_value_lo` (`ENABLE_BOR_DIAG_MARKER=1`)

После reset прошивка лочит в **value_lo** последний этап секвенсера (коды `0xE1`…`0xE6`, не обычный `POWER_CTRL`). В `.noinit` хранится пара `marker` + `~marker`. Парсер / `lib.sh` → строка `bor_diag=…`.

Если после BL reset видите `value_lo=0x00` и `boot_counter` — случайное большое число, **SRAM при BOR не удержала** даже breadcrumb; тогда остаётся только `mask_lo=0x00` + осциллограф на 3.3V.

| value_lo | bor_diag     | Этап |
|----------|--------------|------|
| `0xE3`   | `scaler_pre` | перед `SCALER_POWER_ON` |
| `0xE4`   | `scaler_on`  | после GPIO SCALER ON |
| `0xE5`   | `lcd_pre`    | перед `LCD_POWER_ON` |
| `0xE6`   | `lcd_on`     | после GPIO LCD ON |
| `0xE1`   | `bl_pre`     | перед BL ON (PWM) |
| `0xE2`   | `bl_on`      | после `BACKLIGHT_ON` HIGH |

При провале BL: `mask_lo=0x00` + `bor_diag=bl_on` → reset на включении подсветки. Если `bor_diag=scaler_on`/`lcd_on` — сбой на более раннем шаге UP.

### Прошивка: grace period и `ENABLE_BACKLIGHT_HW`

- **`SEQ_BL_PGOOD_GRACE_MS`** (1000 ms) — после GPIO `BACKLIGHT_ON` HIGH игнорируется краткий провал PGOOD в `dseq_process()`.
- **`bl_gpio_on_ts`** — sentinel `UINT32_MAX` = «не установлено» (защита от ложного grace при `ts=0`).
- **`ENABLE_BACKLIGHT_HW`** в `config.h`:
  - `1` — BL ON разрешён (анализ / нормальная работа);
  - `0` — BL ON отклоняется с ACK `status=0x01` (временная защита от BOR-loop на неисправной ревизии).

### UART-тесты (`Tests_UART_All`)

```bash
bash Tests_UART_All/03_display_scaler_lcd_on.sh   # база: state=0x03 или 0x07 после auto-start
bash Tests_UART_All/05_backlight_brightness.sh    # ключевой BL-тест
```

При `ENABLE_BACKLIGHT_HW=0` suite ставит **SKIP** на BL-зависимые скрипты (`05`, `08`, `11`–`15`), SCALER+LCD (`03`, `04`, `06`, `09`) продолжают выполняться.

### Аппаратный fix (после подтверждения BOR)

1. Осциллограф на **3.3V_A** в момент `BACKLIGHT_ON` → HIGH.
2. Ёмкость / ESR на ветке MCU, развязка возвратных токов BL.
3. Ограничение inrush BL-источника (soft-start, токоограничение).
4. После fix: `ENABLE_BACKLIGHT_HW=1`, повтор `05_backlight_brightness.sh` → ожидание `state=0x07`, `last_power_ctrl_mask_lo≠0x00`.

---

## Быстрый справочник: кадры UART

| Команда                             | Hex кадр                     | Ожидаемый ответ             |
| ----------------------------------- | ---------------------------- | --------------------------- |
| PING                                | `02 01 00 15 03`             | `02 01 01 AA 21 03`         |
| GET_STATUS                          | `02 04 00 54 03`             | 31 байт                     |
| RESET_FAULT                         | `02 05 00 BF 03`             | `02 05 01 00 BB 03`         |
| RESET_BRIDGE                        | `02 06 00 A8 03`             | `02 06 01 00 AC 03`         |
| BOOTLOADER_ENTER                    | `02 08 00 9E 03`             | `02 08 01 00 9A 03` + reset |
| CALIBRATE_OFFSET                    | `02 09 00 89 03`             | `02 09 01 00 8D 03`         |
| BL_OFF только (mask=0x04, val=0x00) | `02 02 04 04 00 00 00 D5 03` | `02 02 01 00 06 03`         |

---

## Расшифровка fault_flags

| Hex      | Биты      | Причина                                  |
| -------- | --------- | ---------------------------------------- |
| `0x0001` | бит 0     | FAULT_SCALER — авария скалера            |
| `0x0002` | бит 1     | FAULT_LCD                                |
| `0x0004` | бит 2     | FAULT_BACKLIGHT                          |
| `0x0008` | бит 3     | FAULT_AUDIO                              |
| `0x0080` | бит 7     | FAULT_PGOOD_LOST                         |
| `0x0100` | бит 8     | FAULT_AMP_FAULTZ                         |
| `0x0200` | бит 9     | FAULT_V24_RANGE                          |
| `0x0400` | бит 10    | FAULT_V12_RANGE                          |
| `0x0800` | бит 11    | FAULT_V5_RANGE                           |
| `0x1000` | бит 12    | FAULT_V3V3_RANGE                         |
| `0x2000` | бит 13    | FAULT_SEQ_ABORT                          |
| `0x4000` | бит 14    | FAULT_INTERNAL                           |
| `0x2001` | биты 0+13 | SEQ_ABORT + SCALER (типично без дисплея) |

stty -F /dev/ttyUSB0 115200 cs8 -cstopb -parenb raw -echo min 0 time 10

exec 3<>/dev/ttyUSB0

printf '\x02\x01\x00\x15\x03' >&3

dd bs=1 count=64 <&3 status=none 2>/dev/null | xxd

exec 3<&-
exec 3>&-
