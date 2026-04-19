# Тест-план на STM32F407G-DISC1 (без целевого железа)

Целевой MCU проекта — **STM32F030R8T6** (Cortex‑M0, 32 МГц). В наличии только отладочная плата **STM32F407G-DISC1** (Cortex‑M4, 168 МГц, встроенный ST‑LINK/V2‑A, акселерометр LIS3DSH, аудио‑ЦАП CS43L22).

Цель этого документа — зафиксировать, **что реально можно проверить на F407**, что — **нельзя**, и как именно это тестировать, **не нарушая контракт** `README.md`.

---

## 0. Главное ограничение

Тестирование на F407 — это **только проверка переносимой бизнес‑логики**. Приёмка прошивки (раздел 17.2 README) возможна **только** на целевом железе с MCU `STM32F030R8T6`, датчиками `NSM2012`, делителями, оптопарами и PGOOD от реальной шины питания.

Причины:

- разный HAL (`stm32f0xx_hal` vs `stm32f4xx_hal`), разный startup/linker
- разный ADC (F0 — один ADC, scan+DMA в ring; F4 — 3 ADC, другая модель)
- на F4 **нет `TIM17`** — есть `TIM1/2/3/4/5/8/9..14` (для BL PWM придётся взять другой таймер)
- разная Flash‑раскладка: F030 — страницы по 1 КБ (`0x0800FC00`), F407 — секторы по 16/64/128 КБ (последний сектор совсем в другом месте)
- системный ROM‑bootloader у F0 и F4 находится по адресу `0x1FFF0000`, но **протоколы и поведение разные** — проверить «в живую» вход в bootloader F030 на F407 нельзя
- на DISC1 нет аналоговых датчиков тока, нет делителей на шину 24 В, нет оптопар, нет усилителя `TPA3118D2`, нет моста `CH7511b`

**Вывод**: на F407 собираем **тестовый подпроект** (отдельный `.ioc`, отдельный build target), куда переносятся **только переносимые модули** (`Protocol/`, `Services/` без HAL‑деталей, `Config/`). Всё, что завязано на конкретный GPIO/ADC канал/TIM — заменяется на тестовые «пины» и DAC‑эмуляцию.

---

## 1. Что можно протестировать на F407 (переносимая логика)

### 1.1. UART‑протокол целиком (раздел 9, 20 README)

Это **чистый C**, не зависит от MCU. Можно и нужно проверять на F407.

Что именно проверяется:

- парсер фрейма `[STX][CMD][LEN][DATA][CRC][ETX]`
- межбайтовый таймаут 10 мс, таймаут пакета 50 мс
- `0x02` (STX) посередине пакета → сброс парсера
- CRC‑8/ATM (`poly=0x07`, `init=0x00`, `refin=false`, `refout=false`, `xorout=0x00`)
- команды `PING`, `POWER_CTRL`, `SET_BRIGHTNESS`, `GET_STATUS`, `RESET_FAULT`, `RESET_BRIDGE`, `SET_THRESHOLDS`, `BOOTLOADER_ENTER`, `CALIBRATE_OFFSET`
- формат ответов ACK (`status=0x00`) / ошибка (`status=0x01`) / NACK (`CMD=0xFF`, `error=0x01`)
- `PING` отвечает строго `status=0xAA`
- фиксированные длины `LEN` (таблица 20.1), в т.ч. `GET_STATUS` = **26 байт** DATA
- Little Endian во всех полях

Как подключать:

- `USART2` F407 (PA2/PA3) через встроенный ST‑LINK USB‑VCP (на DISC1 он не разведён на VCP — нужен внешний USB‑UART, например CP2102/CH340, к PA2/PA3 3.3 В)
- либо использовать `USART6` (PC6/PC7) и вынести провода на Morpho

Скрипт тестирования на стороне ПК:

- `pyserial` + `crc8` (или самописный CRC‑8/ATM)
- гонять 10 000+ случайных пакетов: валидные, с битым CRC, с «разорванной» серединой, с `0x02` внутри `DATA` — парсер обязан не уходить в рассинхрон

### 1.2. ADC → мВ → физ. величина (разделы 2.3–2.4 README)

На F407 у ADC `Vref+` обычно 3.0 В (от LD3985) — **поэтому формула `raw*2500/4096` даст заниженные значения**, и это нормально: смысл теста не в «получить 24 В из делителя», а **проверить, что функция пересчёта арифметически корректна**.

Проверка двухуровневая:

1. **Юнит‑тест функций пересчёта** (host‑сборка под `gcc -O0`, без HAL):
   - `adc_raw_to_mv(raw)` при `VREF=2500`, `RES=4096`
   - `mv_to_voltage(adc_mv)` с коэффициентом `11616/1000`
   - `mv_to_current(adc_mv, Voffset_mV)` с `264 мВ/А` (знаковый результат)
   - граничные случаи: `adc=0`, `adc=4095`, `adc=Voffset`, `adc>Voffset` (положит. ток), `adc<Voffset` (отриц.)
   - что `Left alignment` при 12‑bit обязан делать `raw>>=4` (если логика это поддерживает)

2. **DAC→ADC loopback на самой F407** (физически):
   - F407 имеет 2‑канальный 12‑bit DAC (`PA4 = DAC_OUT1`, `PA5 = DAC_OUT2`)
   - джампером соединить `PA4 (DAC_OUT1)` ↔ любой аналоговый вход ADC (например, `PA6`)
   - программно задавать DAC в диапазоне 0..VREF и проверять, что прочитанное через ADC + формула даёт то же напряжение ±1 LSB + шум
   - это тест «АЦП живой + DMA scan работает + фильтр 8 отсчётов корректен»

Что **не** проверяется: реальный делитель 4.99k/470 (его на плате нет), точный VDDA=2.5 В (на F407 VDDA ≈ 3.0 В), клиппинг токового датчика на 2500 мВ.

### 1.3. Фильтрация ADC и подтверждение аварии (раздел 7 README)

Эта логика полностью в C, без HAL. Можно и нужно закрыть юнит‑тестами:

- скользящее окно 8 отсчётов → корректное среднее
- **подтверждение только по 5 последовательным превышениям подряд** (не «5 из 8»)
- любое значение в норме **сбрасывает** consecutive counter
- после подтверждения fault — **latched**, без автосброса

Формат теста: прогнать массив из N значений и сверить fault_flags на каждом шаге.

### 1.4. Power Manager state machine — логически (разделы 13.5–13.7 README)

На F407 нет оптопар и нагрузок, но можно:

- заменить реальные `HAL_GPIO_WritePin(*_POWER_ON, …)` на запись в тестовый массив «состояние железа»
- заменить реальный ADC‑замер `SCALER_POWER_M/LCD_POWER_M/BL_POWER_M` на DAC‑эмуляцию:
  - через DAC генерируем «плавный подъём» до порога 4000/2800/9000 мВ → убеждаемся, что секвенс нормально дошёл до `SEQ_UP_DONE`
  - задерживаем «подъём» больше 200 мс → секвенс обязан отработать `SEQ_VERIFY_TIMEOUT` → `FAULT_*` + emergency display off
- User button DISC1 (`PA0`, active HIGH при нажатии) использовать как «PGOOD»:
  - «кнопка не нажата» = PGOOD LOW → при старте через 5 с должен взводиться `FAULT_PGOOD_LOST`
  - «кнопка нажата до старта» = PGOOD HIGH → `power_auto_startup()` идёт до конца
  - отпускание кнопки в середине секвенса → `SEQ_ABORT` + emergency off

Что проверяется:

- отсутствие `HAL_Delay` в секвенсе (тест: `power_manager_process()` должен возвращаться быстрее 1 мс на любой итерации)
- `BACKLIGHT=ON` запрещён, если SCALER или LCD выкл → `status=1`, состояние без изменений
- запрос `SCALER=OFF` при включённом BL → сначала корректный shutdown (PWM=0 → BL=LOW → LCD=LOW → RST=LOW → SCALER=LOW) и только потом применение
- таблица 19.4.2 «fault → отключаемые домены»

Что **не** проверяется: реальные тайминги оптопар, реальные реакции `CH7511b` (моста физически нет).

### 1.5. Fault Manager (разделы 7, 19.4 README)

Полностью переносимо. Юнит‑тестами проверить:

- каждая комбинация `fault_flags` → корректная доменная маска принудительного отключения (таблица 19.4.2)
- `RESET_FAULT (0x05)` очищает `fault_flags`, но **сам ничего не включает**
- при `FAULT_PGOOD_LOST`, `FAULT_Vxx_RANGE`, `FAULT_INTERNAL` выключаются **все** домены
- при `FAULT_SCALER` выключаются `SCALER+LCD+BACKLIGHT`, при `FAULT_LCD` — `LCD+BACKLIGHT`

### 1.6. Входы и debounce (раздел 4.4, 16 README)

- частота опроса 1 кГц через SysTick
- debounce 20 мс
- User button DISC1 = один из `IN_x` в тесте
- проверка: короткие «дребезги» (одиночные 0/1 быстрее 20 мс) не должны менять состояние; стабильное удержание ≥ 20 мс — меняет

### 1.7. SUS_S3# автозапуск (раздел 6.2 README)

Тоже можно эмулировать кнопкой:

- «кнопка отжата» = SUS_S3# LOW
- таймер 500 мс → формируется импульс PWRBTN# LOW 150 мс (имитация — зажигание одного из LED на время импульса)
- повторы не чаще 1 раза в 5 секунд — проверяется осциллографом или логическим анализатором на пине

### 1.8. PWM яркости — частота 200 Гц (раздел 5 README)

На F407 нет `TIM17`, но есть много других таймеров. Для теста:

- поднять любой advanced/general‑purpose таймер с CH1 на пине, доступном на Morpho (например, `TIM3 CH1` на `PB4` или `TIM4 CH1` на `PB6`)
- PWM 200 Гц, диапазон CCR 0..1000
- проверяется:
  - `SET_BRIGHTNESS (0x03)` с pwm=0 → скважность 0% (линия LOW)
  - pwm=1000 → скважность 100% (линия HIGH)
  - pwm=500 → 50%
  - полярность `Active High` (важно по разделу 9.643 README: «0% = подсветка выключена»)

**Важно**: это только проверка арифметики `SET_BRIGHTNESS → CCR` и полярности, **не** подтверждение работы с реальной подсветкой.

### 1.9. IWDG (раздел 15 README)

IWDG есть и на F4. Проверяется:

- `HAL_IWDG_Refresh(&hiwdg)` стоит **ровно в одной точке** main loop, после UART/ADC/state/fault (grep по исходникам: ровно 1 вхождение `HAL_IWDG_Refresh` в main loop, 0 в ISR/callback)
- если искусственно заблокировать main loop `while(1);` после входа в WHILE — MCU ушёл в reset через ~1 с
- если кормить watchdog из SysTick — нарушение контракта, этот антипаттерн специально проверить и **убедиться что его нет**

### 1.10. Bootloader jump по SRAM magic (раздел 19.5 README) — **только логика**

На F407 можно проверить, что:

- `BOOTLOADER_ENTER (0x08)` → MCU отвечает ACK, ждёт TX complete, пишет магик в SRAM, делает `NVIC_SystemReset`
- после reset в самом начале `main()` проверяется магик по фиксированному адресу SRAM, и при совпадении фиксируется ветка bootloader (без исполнения ROM‑jump на F407)

**Нельзя** проверить, что после прыжка реально оживает системный загрузчик **STM32F030**: адрес `0x1FFF0000` у F0 и F4 содержит разный ROM‑bootloader с разным протоколом. То есть «вошли в bootloader ROM» можно проверить **только на целевом F030**. На F407 мы убеждаемся лишь в корректной работе механизма «магик → reset → детект».

### 1.11. Калибровка offset — **только формат данных**

На F407 Flash совсем другой (секторы, не страницы), поэтому физическая запись по адресу `0x0800FC00` не имеет смысла. Но можно:

- проверить формат структуры (magic, version, reserved, 5× raw, CRC32) — юнит‑тест
- проверить поведение при невалидном CRC → использовать дефолтный offset 1650 мВ
- физическую запись/стирание страницы проверять **только на F030**

---

## 2. Что **нельзя** проверить на F407 (обязательно — только на целевом железе)

- реальный power sequencing дисплея с реакцией оптопар, скалера, LCD, моста `CH7511b`
- реальные токи через датчики `NSM2012‑05B3R‑DSPR` (offset, gain, ratiometric поведение при «плавающем» +3.3V_A)
- клиппинг АЦП при VDDA=2.5 В (на F407 VREF ≈ 3.0 В)
- реальный `PGOOD` от системы питания (на DISC1 он эмулируется кнопкой)
- `Faultz` усилителя `TPA3118D2` и его последовательности mute/SDZ/POWER
- реальный RST_CH7511b открытого стока с внешней подтяжкой 100 кОм
- вход в системный ROM‑bootloader **STM32F030** (у F4 другой ROM, другой протокол)
- запись калибровки во Flash по адресу `0x0800FC00` (у F4 там ничего нет/другой сектор)
- автозапуск Linux через реальный `PWRBTN#` (на DISC1 только визуализация импульса)
- поведение при реальных сбоях по цепям +24/+12/+5/+3.3 (нет делителей, нет шин)

Эти пункты **не закрыты** тестированием на F407 и должны быть явно помечены как «только на целевом железе» в критериях приёмки (раздел 17.2 README).

---

## 3. Что дают акселерометр и ЦАП (дополнительно)

### 3.1. Акселерометр `LIS3DSH` (SPI) — **полезен слабо**

В целевом проекте SPI **не используется**. Акселерометр можно применить только как «источник правдоподобно шумящих данных» для отладки фильтра ADC (окно 8, подтверждение 5 подряд), если подать его значения в обход АЦП. Но это искусственный случай, реальной ценности для проверки контракта нет. **Рекомендую не использовать**, чтобы не размывать фокус тестирования.

### 3.2. Аудио‑ЦАП `CS43L22` (I2C/I2S) — **не полезен**

Целевой проект использует внешний усилитель `TPA3118D2` с простыми GPIO (`MUTE`, `SDZ`, `POWER_AUDIO`), **без I2S**. `CS43L22` проверяет только свой собственный тракт DISC1 и никак не относится к логике `POWER_AUDIO/MUTE/SDZ` контракта. **Не использовать.**

### 3.3. Встроенный DAC STM32F407 (`PA4`, `PA5`) — **очень полезен**

Именно он закрывает самую важную часть тестирования без железа:

- генерация «напряжения питания» на вход АЦП (loopback DAC→ADC) для проверки всего тракта: DMA scan → фильтр 8 → пересчёт мВ → сравнение с порогом → latched fault
- моделирование «плавного подъёма напряжения» в display power sequencing (раздел 13.2):
  - DAC 0 → 4000 мВ за < 50 мс — ADC «видит» SCALER как ON, секвенс идёт дальше
  - DAC удерживается на 3000 мВ → ADC не видит порог 4000 мВ → через 200 мс `FAULT_SCALER` + emergency off
- моделирование «просадки» питания в рантайме:
  - DAC уронить ниже `V24_MIN` → после 5 подряд замеров `FAULT_V24_RANGE` → выключение **всех** доменов
- моделирование превышения тока:
  - DAC подать `Voffset + 264*3 = 2442 мВ` → ток ≈ 3 А → превышение `I_LCD_MAX=2000 мА` → 5 подтверждений → `FAULT_LCD` → выкл `LCD+BACKLIGHT`

Это наиболее честное приближение к целевому сценарию, доступное без реального железа.

### 3.4. User button (`PA0`) и 4 LED (`PD12..PD15`) — **полезны**

- `PA0` → эмуляция `PGOOD` или `SUS_S3#` или любого из `IN_x`
- LED1 (зелёный, PD12) — индикация `SCALER=ON`
- LED2 (оранжевый, PD13) — индикация `LCD=ON`
- LED3 (красный, PD14) — индикация `BACKLIGHT=ON`
- LED4 (синий, PD15) — индикация FAULT (любой бит в `fault_flags`)

Это заменяет отсутствующий дисплей/плату и позволяет визуально отлаживать state machine.

---

## 4. Минимальный стенд (рекомендуемый)

1. DISC1 + кабель micro‑USB (питание + ST‑LINK + SWD)
2. USB‑UART (CP2102/CH340, 3.3 В) на `USART2` (PA2 TX MCU → RX преобразователя, PA3 RX MCU ← TX преобразователя). **Общий GND обязателен.**
3. Джампер: `PA4 (DAC_OUT1)` → `PA6` (любой свободный ADC вход) — для loopback
4. (опционально) логический анализатор Saleae/DSLogic на `BL_PWM` пин и эмуляцию `PWRBTN#` — для проверки таймингов 200 Гц / 150 мс / 5 с
5. Python 3 + `pyserial` на хосте для скрипта‑тестера протокола

---

## 5. Структура тестового подпроекта (предложение)

```
/test_f407/            ← отдельный каталог, отдельный .ioc, отдельный build
  .ioc                 ← STM32F407VGTx, HSE=8 МГц, SYSCLK=168 МГц
  Core/                ← CubeMX stub
  Drivers/             ← STM32F4 HAL
  Services/
    adc_service.c      ← копия, маппинг каналов под F4 (DAC→ADC loopback)
    input_service.c    ← копия, PGOOD=PA0 (кнопка)
    power_manager.c    ← копия, GPIO переопределены на LED1..4 + выведенные пины
    fault_manager.c    ← без изменений
    uart_protocol.c    ← без изменений (чистая C)
    flash_cal.c        ← stub (на F4 Flash не трогаем, только проверка формата в RAM)
    bootloader.c       ← логика магика+reset+detected; ROM jump на F407 не выполняем
  Protocol/            ← без изменений
  Config/
    config.h           ← те же пороги/константы, включая ADC_VREF_MV = 2500 (контракт), остальное как есть
  tests/
    host/              ← юнит‑тесты на хосте (gcc -O0), без HAL
    python/            ← pyserial‑тесты протокола

```

**Важно**: целевой проект под F030 остаётся нетронутым и собирается своим CubeMX + Makefile. Тестовый проект под F407 — отдельная ветка/каталог; общий только C‑код переносимых модулей (через симлинки или git‑сабмодуль).

---

## 6. Чеклист «что реально будет проверено» (DoD для F407)

- [ ] UART: 10 000 случайных пакетов без рассинхрона, CRC валидируется корректно
- [ ] UART: все 9 команд возвращают корректные `LEN` и данные (таблица 20.1)
- [ ] UART: `PING` всегда `status=0xAA`, неизвестная команда → `CMD=0xFF, error=0x01`
- [ ] `GET_STATUS` DATA = ровно 26 байт, порядок полей по 20.2
- [ ] `SET_BRIGHTNESS`: pwm=0 → 0% (LOW), pwm=1000 → 100% (HIGH), полярность active‑HIGH
- [ ] ADC DMA ring 14 элементов, порядок каналов по таблице раздела 7 README
- [ ] Юнит‑тесты пересчёта raw→мВ→V/мА (включая отрицательный ток)
- [ ] Фильтр 8 отсчётов + подтверждение 5 подряд, сброс counter’а при одном «в норме»
- [ ] DAC→ADC loopback: подъём/провал/превышение приводит к ожидаемому `FAULT_*`
- [ ] Fault latched, `RESET_FAULT` чистит флаги, домены остаются OFF
- [ ] `POWER_CTRL`: BL=ON запрещён без SCALER+LCD; shutdown sequencing при попытке выключить SCALER при BL=ON
- [ ] PGOOD‑кнопка: при LOW 5 с → `FAULT_PGOOD_LOST`; при HIGH → `power_auto_startup`
- [ ] SUS_S3‑эмуляция: импульс 150 мс на индикаторном LED, cooldown 5 с
- [ ] IWDG: refresh ровно в одной точке main loop; искусственное зависание → reset через ~1 с
- [ ] Debounce входов: игнор < 20 мс, срабатывание ≥ 20 мс
- [ ] `BOOTLOADER_ENTER`: ACK → TX complete → магик в SRAM → reset → детект (на F407 фиксируем факт детекта; ROM‑jump STM32F030 здесь не проверяется и не исполняется)

---

## 7. STM32CubeMX 6.17.0 — конфигурация тестового проекта на F407

Ниже — **пошаговая** настройка отдельного `.ioc` под DISC1 (тестовый подпроект `test_f407/`). Основной `.ioc` под `STM32F030R8T6` **не трогаем**.

### 7.1. Версии (зафиксировано)

- **STM32CubeMX**: **6.17.0** (та же, что и у основного проекта — см. раздел 17.1 README)
- **STM32CubeF4 (HAL package)**: установить последний доступный в CubeMX (типично `V1.28.x`) через `Help → Manage embedded software packages`
- **Toolchain**: `GNU Arm Embedded` (тот же, что для F030), сборка через **Makefile**
- **Target MCU**: `STM32F407VGTx`, корпус `LQFP100`
- **Board**: по желанию можно стартовать через `Board Selector → STM32F4DISCOVERY` — тогда LED/кнопка/HSE сразу получат корректные пины. Дальше конфиг дорабатывается вручную под тестовый стенд.

### 7.2. Создание проекта

1. `File → New Project → Board Selector → STM32F4DISCOVERY → Start Project`
2. На запрос «Initialize all peripherals with their default Mode» — **No** (чтобы ничего лишнего типа USB OTG / I2S / CS43L22 не включилось).
3. `Project Manager → Project`:
   - `Project Name`: `test_f407`
   - `Project Location`: `<репо>/test_f407/`
   - `Toolchain/IDE`: **Makefile**
   - `Linker → Stack Size`: `0x600`
   - `Linker → Heap Size`: `0x100`
4. `Project Manager → Code Generator`:
   - **Copy only the necessary library files**: ON
   - **Generate peripheral initialization as a pair of '.c/.h' files per peripheral**: ON
   - **Keep User Code when re-generating**: ON
   - **Delete previously generated files when not re-generated**: ON
5. `Project Manager → Advanced Settings`:
   - `HAL` для всех периферий (не LL)
   - `Register Callback`: все `DISABLE` (как в основном проекте)

### 7.3. System Core

#### RCC (тактирование, SYSCLK = 168 МГц)

- `System Core → RCC`:
  - `High Speed Clock (HSE)`: **Bypass clock source** (ST‑LINK на DISC1 подаёт готовые 8 МГц по MCO на `PH0‑OSC_IN`). Если DISC1 с ревизией `MB997C` — стоит именно Bypass, иначе кварц.
  - `Low Speed Clock (LSE)`: **Disable** (не нужен для теста)
- Clock Configuration (закрепить вручную):
  - `Input frequency`: **8 МГц**
  - PLL Source: **HSE**
  - `PLLM = 8`, `PLLN = 336`, `PLLP = /2`, `PLLQ = 7`
  - `SYSCLK = 168 МГц`
  - `AHB Prescaler = /1` → HCLK = 168 МГц
  - `APB1 Prescaler = /4` → PCLK1 = 42 МГц, `APB1 Timer = 84 МГц`
  - `APB2 Prescaler = /2` → PCLK2 = 84 МГц, `APB2 Timer = 168 МГц`
  - `Flash Latency`: 5 WS (выставляется автоматически)

> Эти значения — стандартные для DISC1 и позволяют сразу использовать все примеры STM32Cube. Меньшая частота допустима, но не рекомендуется (сбивает тайминги примеров и замеров).

#### SYS

- `System Core → SYS`:
  - `Debug`: **Serial Wire** (SWD через встроенный ST‑LINK)
  - `Timebase Source`: **SysTick** (НЕ брать TIMx — иначе сломается `systick_ms` контракт)

### 7.4. UART для тестбенча (USART2)

Тест‑скрипт на хосте подключается сюда через внешний USB‑UART 3.3 В (или перемычкой на ST‑LINK SWO/VCP, если ваш DISC1 поддерживает).

- `Connectivity → USART2`:
  - `Mode`: **Asynchronous**
  - `Baud Rate`: `115200`
  - `Word Length`: `8 Bits (including Parity)`
  - `Parity`: `None`
  - `Stop Bits`: `1`
  - `Data Direction`: `Receive and Transmit`
- Пины:
  - `PA2` → `USART2_TX` (GPIO Label: `TEST_UART_TX`)
  - `PA3` → `USART2_RX` (GPIO Label: `TEST_UART_RX`)
  - `GPIO speed`: `Low`
- `NVIC Settings → USART2 global interrupt`: **Enable** (priority 3)

> Почему USART2, а не USART1: PA2/PA3 на DISC1 свободны и выведены на Morpho. USART1 (PA9/PA10) на DISC1 занят USB OTG VBUS/ID.

### 7.5. ADC1 + DMA (scan, circular) — DAC→ADC loopback

Цель — воспроизвести 14‑канальный контракт основного проекта в части DMA‑буфера и индексации. На DISC1 нет всех 14 физических сигналов, поэтому используем один физический ADC‑канал, но в регулярной группе оставляем **14 рангов** (контрактный буфер `adc_dma_buf[14]`).

- `Analog → ADC1`:
  - `IN6`: **IN6 Single‑ended** — пин `PA6` (вход для loopback с DAC_OUT1)
  - `Parameters`:
    - `Clock Prescaler`: `PCLK2 div 4` (ADCCLK = 21 МГц, в пределах 36 МГц)
    - `Resolution`: **12 bits**
    - `Data Alignment`: **Right alignment** (критично! — см. раздел 2.4 README)
    - `Scan Conversion Mode`: **Enabled**
    - `Continuous Conversion Mode`: **Enabled**
    - `Discontinuous Conversion Mode`: Disabled
    - `DMA Continuous Requests`: **Enabled**
    - `End Of Conversion Selection`: `EOC flag at the end of all conversions`
    - `Number Of Conversion`: `14` (обязательно для проверки контрактного буфера DMA и индексов 0..13)
    - `Rank 1 → Channel 6`, `Sampling Time`: `480 Cycles` (самый длинный, аналогично основному проекту)

Ранги 1..14 в тестовом проекте задаются для заполнения всего `adc_dma_buf[14]`. Практически можно назначить все ранги на канал 6 (CubeMX это допускает), либо часть рангов на внутренние каналы (`Vrefint`/`Temp sensor`/`Vbat`) при необходимости.

- `DMA Settings`:
  - `DMA2 Stream 0`, `Channel 0`, `Request: ADC1`
  - `Direction`: `Peripheral To Memory`
  - `Mode`: **Circular**
  - `Increment Address`: `Memory` (ON), `Peripheral` (OFF)
  - `Data Width`: `Half Word` / `Half Word`
  - `Priority`: `High`
- `NVIC Settings`: `DMA2 Stream 0 global interrupt` — Enable (priority 1)
- `ADC1 global interrupt`: Enable (priority 2)

### 7.6. DAC — источник тестовых сигналов

- `Analog → DAC`:
  - `OUT1 Configuration`: **Connected to external pin only** → пин `PA4`
  - `Output Buffer`: `Enable`
  - `Trigger`: `None` (обновляем значение программно из main loop)
  - (опционально) `OUT2` на `PA5` — второй независимый источник

**Конфликты DISC1 (важно!)**:

- `PA4` на DISC1 идёт на `I2S3_WS` аудио‑ЦАПа `CS43L22`. Чтобы `CS43L22` не «жрал» линию:
  - `PD4 = CS43L22 Reset`: сконфигурировать как `GPIO Output`, **стартовое состояние LOW** (чип в permanent reset, I2S отключен)
- `PA5` на DISC1 идёт на `SPI1_SCK` акселерометра `LIS3DSH`:
  - `PE3 = LIS3DSH CS`: сконфигурировать как `GPIO Output`, **стартовое состояние HIGH** (SPI транзакций нет, чип игнорит линию)

Без этих двух пинов DAC будет «драться» с внешними чипами и давать искажённый сигнал.

### 7.7. BL_PWM — замена TIM17 (на F4 нет TIM17)

Контракт: 200 Гц, CCR 0..1000, Active HIGH.

- `Timers → TIM4`:
  - `Clock Source`: `Internal Clock`
  - `Channel 1`: **PWM Generation CH1**
  - `Parameters`:
    - `Prescaler`: `419` (таймер TIM4 тактируется от APB1 Timer = 84 МГц, `84 МГц / (419+1) = 200 кГц`)
    - `Counter Mode`: `Up`
    - `Counter Period (AutoReload)`: `999` (`200 кГц / 1000 = 200 Гц`)
    - `Auto‑reload preload`: `Enable`
    - `PWM Mode`: **PWM Mode 1**
    - `OC Polarity`: **High** (active HIGH — критично для команды `SET_BRIGHTNESS`)
    - `Pulse (CCR)`: `0` (стартовая скважность 0%)
    - `Fast Mode`: Disable
- Пин: `PB6` → `TIM4_CH1` (GPIO Label: `BL_PWM_TEST`). Это пин выведен на Morpho CN5 DISC1.

> Альтернатива: `TIM3_CH1` на `PA6` — но этот пин уже занят ADC loopback‑ом. Поэтому берём именно `TIM4` на `PB6`.

### 7.8. IWDG

Контракт (раздел 15 README): таймаут ~1 с.

- `System Core → IWDG`:
  - `Mode`: **Activated**
  - Расчёт: `LSI ≈ 32 кГц`, `Prescaler = 32`, `Reload = 999` → `T ≈ (999+1) * 32 / 32000 = 1 с`
  - `IWDG counter clock Prescaler`: `32`
  - `IWDG down‑counter reload value`: `999`
  - `IWDG window value`: `4095` (окно отключено)

### 7.9. GPIO — индикация и эмуляция сигналов

Контракт тестового стенда — раздел 3.4 и 4 этого файла.

| Пин   | Режим            | GPIO Label      | Назначение в тесте |
| ----- | ---------------- | --------------- | ------------------ |
| PD12  | Output PP, LOW   | `LED_SCALER`    | LD4 green → индикация `SCALER=ON` |
| PD13  | Output PP, LOW   | `LED_LCD`       | LD3 orange → индикация `LCD=ON` |
| PD14  | Output PP, LOW   | `LED_BL`        | LD5 red → индикация `BACKLIGHT=ON` |
| PD15  | Output PP, LOW   | `LED_FAULT`     | LD6 blue → индикация `fault_flags != 0` |
| PA0   | Input, No pull   | `PGOOD_EMU`     | User button B1 (active HIGH при нажатии) — эмулирует `PGOOD` |
| PE0   | Input, Pull‑up   | `IN_0_EMU`      | свободный пин Morpho для эмуляции `IN_0` |
| PE1   | Input, Pull‑up   | `IN_1_EMU`      | эмуляция `IN_1` |
| PE2   | Output PP, HIGH  | `PWRBTN_EMU`    | визуализация импульса `PWRBTN#` (логический анализатор) |
| PD4   | Output PP, LOW   | `CS43L22_RESET` | держать аудио‑ЦАП в reset (см. 7.6) |
| PE3   | Output PP, HIGH  | `LIS3DSH_CS`    | не трогать SPI акселерометра (см. 7.6) |

Для всех `GPIO Output` выставить `GPIO speed = Low` и корректное `Initial level` (как в колонке выше), чтобы при старте было безопасное состояние.

### 7.10. NVIC — приоритеты

Контракт: ISR минимальны, логика в main loop.

| IRQ                        | Priority |
| -------------------------- | -------- |
| SysTick                    | 0        |
| DMA2 Stream0 (ADC1)        | 1        |
| ADC1                       | 2        |
| USART2                     | 3        |

Остальные прерывания (`HardFault`, `NMI`, `SVC`, `PendSV`) — значения по умолчанию.

### 7.11. Что НЕ включать в .ioc (чтобы не тащить лишнее)

На DISC1 соблазн включить всё подряд — **не надо**. Для тестирования контракта строго:

- **USB OTG FS / HS**: Disable
- **I2S2/I2S3**: Disable (иначе CS43L22 захватит линии)
- **SPI1**: Disable (иначе LIS3DSH начнёт отвечать на запросы)
- **I2C1**: Disable (I2C в контракте не используется — раздел 3.3 README)
- **Ethernet**: Disable
- **FSMC**: Disable
- **CAN1/2**: Disable
- **RTC**: Disable (не нужен, LSE отключён)

### 7.12. Что обязательно добавить в `USER CODE` после генерации

CubeMX для F4, как и для F0, **не** вставит `HAL_IWDG_Refresh` в main loop автоматически. Поэтому после первой генерации и каждой последующей — проверить вручную:

- в `Core/Src/main.c`, секция `/* USER CODE BEGIN WHILE */ … /* USER CODE END WHILE */`, **ровно в одной точке** main loop после `uart_protocol_process() / adc_service_process() / input_service_process() / power_manager_process() / fault_manager_process()` вызвать `HAL_IWDG_Refresh(&hiwdg);`
- НЕ вызывать refresh из `SysTick_Handler`, `HAL_*_Callback`, `USART2_IRQHandler`, `DMA2_Stream0_IRQHandler`
- запустить `HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_get_dma_buf(), ADC_CHANNEL_COUNT);` (обратите внимание: `hadc1`, не `hadc` — различие F0/F4)
- запустить `HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);` (вместо `htim17`)
- запустить DAC: `HAL_DAC_Start(&hdac, DAC_CHANNEL_1);` + `HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 0);`

### 7.13. Отличия `config.h` тестового проекта

Переопределения — только там, где это не ломает неизменяемые инварианты контракта. `ADC_VREF_MV` в тестовом проекте **не переопределяется** и остаётся `2500`.

```c
#define ADC_CHANNEL_COUNT    14      /* контракт сохраняем, каналы 0..13 маппятся на IN6 в тесте */
```

Все пороги и константы контракта (`ADC_VREF_MV`, `THRESH_*`, `SEQ_*`, `FAULT_*`, `PROTO_*`, `CMD_*`) **не меняем**. Особенности стенда F407 учитываются только в интерпретации результатов аппаратных измерений, а не через изменение контрактных констант.

### 7.14. Сборка и прошивка

- `make -j` в каталоге `test_f407/`
- Прошивка через встроенный ST‑LINK DISC1:

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c "init; reset halt; program build/test_f407.elf verify reset exit"
```

> Обратите внимание: `target/stm32f4x.cfg`, а не `stm32f0x.cfg`. Для основного F030‑проекта — наоборот.

---

## 8. Критично: эти тесты **не заменяют** приёмку на целевом F030

Всё, что перечислено в разделе 2 («нельзя проверить на F407»), обязано быть повторно проверено на `STM32F030R8T6` в составе реального устройства **до** релиза. Тест‑план DISC1 снижает риск ошибок в переносимой логике и протоколе, но не снимает ответственность за финальные замеры на железе.
