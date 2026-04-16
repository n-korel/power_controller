# Rules / Контекст проекта POWER_Controller (для AI)

Этот файл — **единственный источник “контекста и правил”** для работы AI с проектом прошивки контроллера питания на **STM32F030R8T6**.  
Основание: `README.md`. Если ниже что-то противоречит коду — **считать, что код неверный** и приводить его к контракту из `README.md` (если пользователь не сказал иначе).

## 1) Что это за проект (кратко и точно)

- **Устройство**: MCU `STM32F030R8T6` (возможна совместимая замена `APM32F030R8T6`, но **сборка/настройки как для STM32**).
- **Подход к разработке**: прошивка пишется с использованием **STM32 HAL**.
- **Роль MCU**: детерминированный embedded-контроллер питания/мониторинга/защиты для системы с модулем **Q7 (Linux)**.
- **Функции**:
  - управление доменами питания (Scaler/LCD/Backlight/Audio/Eth1/Eth2/Touch)
  - строгое **power sequencing** дисплея
  - непрерывный мониторинг напряжений/токов/температур (температуры сейчас резерв)
  - защита + фиксация (latched) аварий
  - протокол команд/телеметрии по **UART0**
  - контроль `SUS_S3#` и авто-нажатие `PWRBTN#` для запуска Linux
  - вход в **ROM-bootloader** по команде (через SRAM magic) + существует аппаратный путь через GPIO-expander со стороны Q7

## 1.1) Правила работы с CubeMX/HAL (чтобы не ломать генерацию)

- Конфигурацию периферии меняем через `.ioc` (CubeMX). Правки “вручную” в автосгенерированных местах не считаются устойчивыми.
- В файлах, которые CubeMX генерирует/перегенерирует (`Core/*`, `Drivers/*`), правки делаем **только** внутри секций `/* USER CODE BEGIN */ ... /* USER CODE END */`.
- В прерываниях держим код минимальным: никаких блокирующих ожиданий и тяжёлой логики; основная обработка — в main loop/state machine.
- Если в `.ioc` включён `IWDG` (`VP_IWDG_VS_IWDG.Mode=IWDG_Activate`), считать обязательной постгенерационной задачей добавление `HAL_IWDG_Refresh(&hiwdg)` **ровно в одном месте** main loop: в конце итерации, после обработки UART/ADC/state/fault.
- `MX_IWDG_Init()` запускает watchdog сразу, но CubeMX **не** добавляет refresh в main loop автоматически. Отсутствие этого вызова приводит к reset на каждом старте, а refresh из ISR/callback может маскировать зависания.
- `HAL_IWDG_Refresh(&hiwdg)` запрещено вызывать из прерываний, HAL callback'ов и любой низкоуровневой/периферийной логики.

## 2) Жёстко зафиксированные аппаратные факты (нельзя “улучшать”)

### 2.1 Тактирование и базовые параметры

- **HSE**: 8 МГц (кварц `Y2`)
- **PLL**: x4
- **SYSCLK**: 32 МГц  
Любые тайминги (UART/ADC/PWM/состояния секвенса) считать исходя из **32 МГц**.

### 2.2 Критично: VDDA АЦП = 2.5 В

По схеме `VDDA` питается от стабилизатора **2.5 В** (IC9 `RS3112-2.5XSF3`).

- Любой пересчёт `ADC raw → mV` делать так:
  - при **ADC Resolution = 12-bit** и **Data Alignment = Right**: `adc_mv = (uint32_t)raw * 2500u / 4096u`
  - если в CubeMX кто-то выставил **Left alignment** (для 12-bit): сначала `raw >>= 4`, затем формула выше (иначе будет ×16)
- Константа: `ADC_VREF_MV = 2500`
- Следствие: токовые датчики питаются 3.3 В и могут клипироваться по АЦП при токах > ~3.2 А. **Пороги по току не ставить выше ~3000 мА** (если пользователь не переопределил).

### 2.3 Делители напряжения

Для измерений `+24/+12/+5/+3.3` используются делители 4.99k / 470Ω:

- \(K = 470 / (4990 + 470) ≈ 0.0860806\)
- `Vin_mV ≈ Vadcin_mV * 11.616`

### 2.4 Температуры Temp0/Temp1 — сейчас не используются

- NTC **не установлены** на текущей ревизии.
- В телеметрии поля сохраняются, но считаются “невалидными”. Рекомендованное значение: `temp0=temp1=-32768`.

### 2.5 I2C и GPIO expander

- В схеме есть I2C GPIO-expander IC17 (`TPT29555-TS5R`), но **им управляет Q7**, не MCU.
- MCU **не инициализирует I2C** и не взаимодействует с IC17.
- IC17 может управлять `NRST` и `BOOT0` MCU на стороне Q7 (аппаратный вход в bootloader).

## 3) GPIO логика и safe state (обязательное поведение)

### 3.1 Определения

- Для push-pull линий доменов питания (active HIGH):
  - `ON = HIGH`, `OFF = LOW`
- Для open-drain линий с `#` (кнопки/reset):
  - **assert** = тянуть `LOW`
  - **release** = “HIGH” на OD (фактически Hi‑Z, линия уходит в HIGH через внешнюю подтяжку)

### 3.2 Safe state на старте (до любых команд и секвенсов)

Обязательная безопасная конфигурация сразу после инициализации:

- Все домены питания (**active HIGH**) = **OFF (LOW)**:
  - `SCALER_POWER_ON=0`, `LCD_POWER_ON=0`, `BACKLIGHT_ON=0`,
  - `POWER_AUDIO=0`, `POWER_ETH_1=0`, `POWER_ETH_2=0`, `POWER_TOUCH=0`
- Усилитель (режимы):
  - `SDZ=0` (shutdown)
  - `MUTE=1` (mute)
- Open-drain линии:
  - `PWRBTN#` и `RSTBTN#` — **release** (лог. “1” на OD)
  - `RST_CH7511b` — по умолчанию удерживать в `LOW` (сброс) до секвенса (см. 6)

## 4) UART0 протокол (контракт MCU ↔ Linux)

### 4.1 UART0 параметры (фиксированные)

- 115200, 8N1, без flow control
- Протокол master-slave: Linux опрашивает/командует, MCU отвечает.

### 4.2 Фрейм пакета

`[STX][CMD][LEN][DATA][CRC][ETX]`

- `STX=0x02`, `ETX=0x03`
- `LEN` — длина только `DATA` (0..255)
- `CRC` — **CRC-8/ATM** по `[CMD][LEN][DATA...]`:
  - poly 0x07, init 0x00, refin/refout false, xorout 0x00
- Endianness: **Little Endian**
- Единицы: напряжение мВ, ток мА, яркость `0..1000`, температуры `0.1°C` (или -32768 если невалидно).

### 4.3 Команды (коды фиксированы)

- `0x01 PING`
- `0x02 POWER_CTRL`
- `0x03 SET_BRIGHTNESS`
- `0x04 GET_STATUS`
- `0x05 RESET_FAULT`
- `0x06 RESET_BRIDGE`
- `0x07 SET_THRESHOLDS`
- `0x08 BOOTLOADER_ENTER`
- `0x09 CALIBRATE_OFFSET`

### 4.4 Ответы (общая политика)

- Успех: ответ с тем же `CMD`, `DATA = uint8 status=0`
- Исключение: `PING (0x01)` отвечает `DATA = uint8 status=0xAA`
- Ошибка параметров/валидации: тот же `CMD`, `DATA = uint8 status=1`
- Неизвестная команда: `CMD=0xFF`, `DATA=uint8 error_code` (минимум `0x01=unknown command`)

### 4.5 Жёсткие требования протокола

- `GET_STATUS` **layout неизменяемый**, `LEN=26`, поля строго в порядке:
  - `v24,v12,v5,v3v3` (uint16)
  - `i_lcd,i_backlight,i_scaler,i_audio_l,i_audio_r` (uint16)
  - `temp0,temp1` (int16)
  - `state` (uint8, битовая маска доменов как в POWER_CTRL)
  - `fault_flags` (uint16, latched)
  - `inputs` (uint8: bit0..5=IN0..IN5, bit6=PGOOD, bit7=Faultz)
- `POWER_CTRL`:
  - Request `LEN=4`: `mask:uint16 + value:uint16`
  - Биты доменов: 0=SCALER, 1=LCD, 2=BACKLIGHT, 3=AUDIO, 4=ETH1, 5=ETH2, 6=TOUCH
  - **BACKLIGHT нельзя включать**, если SCALER или LCD выключены (вернуть status=1).

## 5) АЦП: порядок, DMA layout и фильтрация (контракт)

### 5.1 Режим

- ADC scan + **DMA circular**
- DMA: `Peripheral increment = Disabled`, `Priority = High`
- Каналов **ровно 14**
- Любая смена порядка каналов ломает контракт (DMA layout, телеметрия, пороги).

### 5.2 Порядок сканирования (Rank 1..14 = DMA index 0..13)

1. `PA0`  LCD_CURRENT_M
2. `PA1`  BACKLIGHT_CURRENT_M
3. `PA4`  SCALER_CURRENT_M
4. `PA5`  AUDIO_L_CURRENT_M
5. `PA6`  AUDIO_R_CURRENT_M
6. `PA7`  LCD_POWER_M
7. `PB0`  BACKLIGHT_POWER_M
8. `PB1`  SCALER_POWER_M
9. `PC0`  V+24_M
10. `PC1` V+12_M
11. `PC2` V+5_M
12. `PC3` V+3.3_M
13. `PC4` Temp0_M (резерв)
14. `PC5` Temp1_M (резерв)

### 5.3 Фильтрация аварий (фиксированная политика)

- Усреднение/окно: 8 измерений
- Подтверждение аварии: **5 подряд** измерений вне порога
- Любое измерение в норме **сбрасывает** consecutive-счётчик

## 6) Display power sequencing (обязательные секвенсы, без блокирующих delay)

### 6.1 Главные запреты

- Нельзя “дёргать GPIO” дисплея напрямую. Все изменения `SCALER/LCD/BACKLIGHT` проходят через state machine секвенса.
- Никаких `HAL_Delay`/блокирующих ожиданий в секвенсе. Только state machine + `systick_ms`.

### 6.2 Предусловия секвенса

- `PGOOD` должен быть `HIGH`. Если `PGOOD=LOW` — секвенс запрещён.
- Если `PGOOD` упал во время секвенса — немедленно аварийно выключить:  
  `BACKLIGHT_OFF → LCD_OFF → RST_LOW → SCALER_OFF`, зафиксировать fault.

### 6.3 Включение дисплея (SCALER→RST release→LCD→BACKLIGHT)

Последовательность (с проверкой напряжений и таймаутами):

- SCALER ON → wait 50ms → проверить `SCALER_POWER_M` (таймаут подтверждения 200ms)
- `RST_CH7511b` release → wait 20ms
- LCD ON → wait 50ms → проверить `LCD_POWER_M` (таймаут 200ms)
- BACKLIGHT ON + PWM start → проверить `BACKLIGHT_POWER_M` (таймаут 200ms)

Рекомендуемые пороги подтверждения (мВ):

- `SCALER_POWER_M` > 4000
- `LCD_POWER_M` > 2800
- `BACKLIGHT_POWER_M` > 9000

### 6.4 Выключение дисплея

- PWM=0 → wait 10ms → BACKLIGHT OFF → wait 20ms → LCD OFF → wait 20ms → RST LOW → SCALER OFF

### 6.5 Интеграция со стартом

После старта MCU ждёт `PGOOD=HIGH`. Если `PGOOD` не появился за 5 секунд — установить `FAULT_PGOOD_LOST` и оставаться в безопасном состоянии.

После `PGOOD=HIGH` MCU **автоматически** поднимает дисплей до:

- `SCALER=ON`, `LCD=ON`, но **`BACKLIGHT=OFF`**
- `TOUCH=ON`
- `AUDIO=ON`, но усилитель остаётся в безопасном состоянии: `SDZ=0`, `MUTE=1`

### 6.6 Интеграция с POWER_CTRL

- Если нужно `BACKLIGHT=ON`: допускается только когда `SCALER=ON` и `LCD=ON` (иначе status=1).
- Если запрошено `SCALER=OFF` или `LCD=OFF`, а `BACKLIGHT=ON` — сначала полный shutdown sequencing.

## 7) Faults: фиксация, биты, политика отключения (контракт для Linux)

### 7.1 Общие правила

- Fault **защёлкивается** (latched): авто-сброса нет.
- `RESET_FAULT` очищает `fault_flags`, но **ничего не включает автоматически**.
- При аварии: **сначала** перевести железо в safe, **потом** отразить fault в `GET_STATUS`.

### 7.2 Карта `fault_flags` (uint16, значения битов фиксированы)

- bit0  `FAULT_SCALER`
- bit1  `FAULT_LCD`
- bit2  `FAULT_BACKLIGHT`
- bit3  `FAULT_AUDIO`
- bit4  `FAULT_ETH1` (резерв)
- bit5  `FAULT_ETH2` (резерв)
- bit6  `FAULT_TOUCH` (резерв)
- bit7  `FAULT_PGOOD_LOST`
- bit8  `FAULT_AMP_FAULTZ`
- bit9  `FAULT_V24_RANGE`
- bit10 `FAULT_V12_RANGE`
- bit11 `FAULT_V5_RANGE`
- bit12 `FAULT_V3V3_RANGE`
- bit13 `FAULT_SEQ_ABORT`
- bit14 `FAULT_INTERNAL`
- bit15 `FAULT_RESERVED` (должен быть 0)

### 7.3 Какие домены отключать при fault (фиксированная политика)

- `FAULT_SCALER` → выключить `SCALER+LCD+BACKLIGHT`
- `FAULT_LCD` → выключить `LCD+BACKLIGHT`
- `FAULT_BACKLIGHT` → выключить `BACKLIGHT`
- `FAULT_AUDIO` или `FAULT_AMP_FAULTZ` → выключить `AUDIO` + перевести усилитель в `MUTE=1, SDZ=0` перед снятием питания
- `FAULT_PGOOD_LOST` → выключить **все** домены
- Любой `FAULT_Vxx_RANGE` → выключить **все** домены
- `FAULT_SEQ_ABORT` → выключить `SCALER+LCD+BACKLIGHT`
- `FAULT_INTERNAL` → выключить **все**

## 8) Авто-запуск Linux (SUS_S3#)

Условие:

- `PGOOD=HIGH` и `SUS_S3#=LOW` более 500ms

Действие:

- импульс на `PWRBTN#` (open-drain) **LOW 150ms**
- повторы не чаще 1 раза / 5 секунд

## 9) Усилитель TPA3118D2 (режимы SDZ/MUTE)

Последовательность (важно для “pop”):

- Включение:
  - `POWER_AUDIO=1` → ждать ≥10ms → `SDZ=1` → через ~10ms `MUTE=0`
- Выключение:
  - `MUTE=1` → пауза → `SDZ=0` → `POWER_AUDIO=0`

Реализация также через state machine (без блокирующих delay).

## 10) Bootloader (обновление прошивки MCU из Linux)

### 10.1 Штатный путь: BOOTLOADER_ENTER

Команда `BOOTLOADER_ENTER (0x08)` должна:

1) перевести всё в safe state  
2) отправить ACK: ответ с тем же `CMD`, `DATA = uint8 status=0` и дождаться завершения передачи  
3) записать SRAM magic (фиксированное значение по фиксированному адресу)  
4) сделать `NVIC_SystemReset()`  
5) после reset: если magic найден — очистить и сделать jump в ROM bootloader `0x1FFF0000` (STM32F030)

### 10.2 Аппаратный путь (для понимания)

Со стороны Q7 через IC17 возможно: `BOOT0=HIGH` + импульс `NRST`, тогда MCU входит в ROM bootloader без участия прошивки.

## 11) Калибровка offset токов во Flash

- Команда `CALIBRATE_OFFSET (0x09)` снимает raw текущих токовых каналов как “0 А” и сохраняет во Flash.
- Резерв под калибровку: **последняя страница Flash** `0x0800FC00` (для STM32F030R8, page 1KB).
- Данные должны быть валидируемы (magic/version + CRC), иначе использовать дефолтный offset.

## 12) Неизменяемые “инварианты” (проверять при любых правках)

AI всегда должен сохранять:

- UART0: 115200 8N1, framing `[02 ... 03]`, CRC-8/ATM
- `GET_STATUS LEN=26` и точный порядок полей
- `PING` всегда отвечает `0xAA`
- `ADC_CHANNEL_COUNT=14` и фиксированный порядок DMA индексов (раздел 5.2)
- `ADC_VREF_MV=2500` (VDDA=2.5V)
- Display sequencing и запрет на прямое выключение SCALER/LCD при включённой подсветке
- Fault latched + `RESET_FAULT` не включает ничего сам
- Safe state на старте (домены OFF, OD “не нажато”, усилитель в shutdown/mute)
- При включённом `IWDG` refresh выполняется ровно в одной точке main loop, после UART/ADC/state/fault, и никогда не выполняется из ISR/callback

## 13) Как AI должен работать с задачами в этом проекте

- Если просят “добавить функционал/исправить баг” — **сначала** убедиться, что изменения не ломают инварианты из раздела 12.
- Если нужна смена протокола/порядка АЦП/таймингов — считать это **изменением контракта** и делать только по явной команде пользователя.
