# План: OTA pending-confirm + персистентность SET_THRESHOLDS

Анализ и план реализации для двух пунктов из бэклога:

1. Подтверждение прошивки после OTA (safe-hold вместо тихого зависания).
2. Персистентность порогов защит (`SET_THRESHOLDS`) во flash.

Основано на текущем состоянии `main` (`85e14bb`): `Services/flash_cal.c/h`,
`Services/fault_manager.c`, `Services/power_manager.c`, `Services/app.c`,
`Services/bootloader.c`, `Protocol/uart_protocol.c`, `STM32F030XX_FLASH.ld`,
`Config/config.h`, `Rules_POWER.md`, `contract/protocol.yaml`.

---

## 0. Общие ограничения, от которых отталкивается план

- Flash — один банк 64 КБ (`ORIGIN = 0x08000000`), сейчас разбит на
  `FLASH (rx) 63K` + `FLASH_CAL (r) 1K`. Второго слота под "золотой" образ
  нет и не появится без внешней памяти на стороне Q7 — то есть **честного
  A/B rollback не будет**, только "подтверди-или-остановись".
- Стиль хранения — уже есть готовый паттерн в `flash_cal.c`: `magic + version
  - payload + crc32`, page erase (`FLASH_TYPEERASE_PAGES`, 1 страница = 1 КБ),
запись словами через `HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, …)`, работа
внутри обработчика команды (без асинхронности) — это уже прецедент
синхронной flash-записи в UART-хендлере (`CALIBRATE_OFFSET`), так что то же
    самое для новых фич ничего не ломает по духу кода.
- `Rules_POWER.md` — "frozen contract", менять только по явной команде
  пользователя. План специально построен так, чтобы **не потребовалось
  менять ни один из пронумерованных инвариантов** — только добавить новые
  файлы/поля поверх существующих.
- **Важно, реальная находка в коде:** бит `FAULT_RESERVED = 0x8000` сейчас
  не используется нигде в прошивке, но `Tests/test_invariants.c:152-153`
  жёстко проверяет, что он всегда `0`. Это не пронумерованный инвариант из
  `Rules_POWER.md`, но это существующий тест, который придётся осознанно
  поменять — см. §1.7.
- Перед стартом реализации: **проверить фактический размер `.text+.rodata+.data`**
  текущей прошивки (`arm-none-eabi-size build/POWER_Controller.elf` после
  `make`). План ниже уменьшает `FLASH` с 63К до 61К (−2 КБ). Тулчейна в
  песочнице нет, я не смог это измерить — это блокер №1 перед тем как трогать
  линкер.

---

## 0.1 Инцидент с предыдущей попыткой (коммит `17d961e0`, откачен)

Коротко, что там сломалось — важно, потому что план ниже не должен наступать
на те же грабли:

1. **Confirm зависел от внешнего шага сборки.** `boot_meta_confirm()` сверял
   CRC32 всего образа с 4-байтовым футером `.fw_crc`, который дописывал
   `fw_sign.py` — но только как побочный эффект цели `make .hex`. Цель
   `flash-stlink` формально зависела от `.hex` (чтобы триггернуть подпись), но
   реально шила `st-flash write firmware.bin` — старый `.bin`, без футера
   (или с футером-плейсхолдером `0xFFFFFFFF`). Любой путь прошивки в обход
   `make hex` (не пересобрался `.hex` по mtime, ручной `make bin`, IDE-флешер,
   `st-flash` напрямую) — и `confirm` гарантированно не проходил, даже для
   полностью исправного образа.
2. **Исчерпание попыток вело к безусловному прыжку в ROM bootloader.**
   `boot_meta_on_startup()` вызывалась в самом начале `app_init()`, и при
   `boot_attempts == 0` сразу дёргала `bootloader_schedule()` — то есть на
   первой же итерации `app_step()` плата сама себя ребутила с magic-меткой, и
   `bootloader_check()` (до `HAL_Init()`) прыгала прямо в заводской ROM
   bootloader, минуя вообще всю прикладную инициализацию: ни экрана, ни
   `uart_protocol_*`. Отсюда симптомы — "не включался экран", "падали UART-
   тесты" (МК отвечает по протоколу ST bootloader, а не по прикладному).
3. **Состояние персистентное и не лечится обычной перепрошивкой.** `boot_meta`
   живёт на отдельной странице, `st-flash write ... 0x08000000` её не
   трогает — попав в цикл, устройство продолжает падать в ROM bootloader даже
   после повторной заливки того же (корректного) образа, пока кто-то не
   сотрёт страницу `boot_meta` вручную.

Чем план в §1 отличается по конструкции — и почему это не формальность:

- **Confirm не зависит ни от какого внешнего инструмента подписи образа.**
  Здесь подтверждение — это либо явный `RESET_FAULT` от Q7 (§1.6), либо
  истечение таймера стабильной работы (§1.5, `BOOT_META_CONFIRM_STABLE_MS`).
  Ни один из механизмов не смотрит на CRC самого образа, футер или путь
  прошивки — `st-flash`, `stm32flash`, IDE-флешер дают одинаковый результат,
  потому что confirm вообще не зависит от содержимого прошивки.
- **Исчерпание попыток НЕ вызывает `bootloader_schedule()`/`NVIC_SystemReset()`.**
  В §1.6 `boot_meta_safe_hold()==1` только пропускает `power_startup_begin()`
  и выставляет fault-бит — вся остальная инициализация (`uart_protocol_init()`,
  ADC, `app_step()`) отрабатывает как обычно. Устройство не "исчезает" в ROM
  bootloader само по себе; попасть туда можно только явной командой
  `BOOTLOADER_ENTER` от Q7, как и в любой другой ситуации. Это нужно
  закрепить отдельным тестом — добавлено в §1.7.
- **Персистентность самой метки во flash — по-прежнему такая же, и это
  осознанно.** `boot_meta` намеренно переживает обычную перепрошивку (иначе
  счётчик попыток не имеет смысла). Но раз подтверждение больше не завязано
  на CRC образа, риск "залипло навсегда даже с исправным образом" уходит
  вместе с причиной (п.1), а не за счёт того, что метка стала менее
  персистентной.

Вывод: прошлый механизм ломался не из-за самой идеи pending-confirm, а
из-за того, что критерий подтверждения не был self-contained (зависел от
конкретного пути сборки/прошивки), и из-за того, что "не подтвердилось"
трактовалось как "безусловно и самостоятельно уйти из прикладного режима".
План ниже убирает оба этих свойства.

---

## 1. Feature 1 — OTA pending-confirm / safe-hold

### 1.1 Проблема

`BOOTLOADER_ENTER` → ROM bootloader → `stm32flash -w -v` верифицирует только
побайтовую запись во flash. Если новый образ логически сломан (виснет,
зацикливается на IWDG, роняет секвенсирование дисплея) — это никак не
отличимо от нормальной работы до следующего `GET_STATUS`/полевого инцидента.

### 1.2 Что делаем (в рамках ограничения "нет второго образа")

Не rollback, а **"подтверди или остановись"**:

- При входе в `BOOTLOADER_ENTER` прошивка помечает во flash "жду
  подтверждения новой прошивки" + счётчик попыток.
- Каждый boot с этой меткой увеличивает счётчик попыток.
- Если попыток больше лимита — прошивка **не выполняет** авто-секвенсинг
  дисплея (`power_startup_begin()` не вызывается), остаётся в safe state,
  но UART/ADC/fault-логика продолжают работать как обычно, и в `fault_flags`
  выставляется новый бит — то есть Q7 сразу видит причину и может: а)
  прочитать `GET_VERSION`, б) перезалить снова через тот же `BOOTLOADER_ENTER`.
- Подтверждение — **без новой UART-команды**, переиспользуем существующий
  `RESET_FAULT` (см. §1.5) + автоматическое подтверждение по таймеру как
  fallback для Q7-софта, который про фичу не знает (см. §1.6).

### 1.3 Flash-раскладка

```
FLASH          (rx) : ORIGIN = 0x08000000, LENGTH = 61K   /* было 63K */
FLASH_BOOT_META(r)  : ORIGIN = 0x0800F400, LENGTH = 1K    /* новое */
FLASH_CAL      (r)  : ORIGIN = 0x0800FC00, LENGTH = 1K    /* без изменений (см. §2 про thresh) */
```

`STM32F030XX_FLASH.ld` — добавить регион и `ASSERT`, аналогично уже
существующему:

```ld
ASSERT(ORIGIN(FLASH) + LENGTH(FLASH) == ORIGIN(FLASH_BOOT_META),
       "FLASH size drifted — FLASH_BOOT_META would overlap firmware")
ASSERT(ORIGIN(FLASH_BOOT_META) + LENGTH(FLASH_BOOT_META) == ORIGIN(FLASH_CAL),
       "FLASH_BOOT_META size drifted — would overlap FLASH_CAL")
```

### 1.4 Структура метаданных (по образцу `flash_cal_t`)

```c
/* Services/boot_meta.h */
typedef struct {
    uint32_t magic;           /* 0..3 */
    uint16_t version;         /* 4..5 */
    uint8_t  pending_confirm; /* 6    1 = новый образ ждёт подтверждения */
    uint8_t  boot_attempts;   /* 7    инкремент на каждом boot пока pending=1 и hash подтверждён (см. §1.8 п.4) */
    uint8_t  max_attempts;    /* 8    снимок BOOT_META_MAX_ATTEMPTS на момент записи */
    uint8_t  tracked_hash[8]; /* 9..16 hash образа (см. FW_GIT_HASH_STR), для которого валиден boot_attempts;
                                  пишется boot_meta_init(), НЕ boot_meta_arm_pending() — см. §1.5, решение по §1.8 п.4 */
    uint8_t  reserved;        /* 17   паддинг под 4-байтовый crc32 */
    uint32_t crc32;           /* 18..21, CRC по байтам 0..17 (unaligned read через memcpy, как в flash_cal.c) */
} boot_meta_t;                /* 22 байта, укладывается в 1 КБ страницы с большим запасом */
```

`Config/config.h` — новые константы:

```c
#define FLASH_BOOT_META_ADDR      0x0800F400U
#define FLASH_BOOT_META_ERASE_SIZE 1024U
#define FLASH_BOOT_META_MAGIC     0x424D4554U /* "BMET" */
#define FLASH_BOOT_META_VERSION   1U

#define BOOT_META_MAX_ATTEMPTS       3U
#define BOOT_META_CONFIRM_STABLE_MS  10000U   /* авто-подтверждение через 10с стабильной работы */

#define FAULT_BOOT_UNCONFIRMED    FAULT_RESERVED  /* переиспользуем 0x8000, см. §1.7 */
```

### 1.5 Новый модуль `Services/boot_meta.c/h`

API:

```c
void    boot_meta_init(void);      /* читает flash, решает pending/attempts, НЕ трогает power_manager */
void    boot_meta_process(void);   /* вызывается из app_step(); авто-confirm по таймеру */
uint8_t boot_meta_safe_hold(void); /* 1 = превышен лимит попыток, авто-старт запрещён */
void    boot_meta_confirm(void);   /* персистентно пишет pending_confirm=0, boot_attempts=0 */
void    boot_meta_arm_pending(void); /* вызывается перед прыжком в ROM bootloader */
```

Логика `boot_meta_init()`:

1. Прочитать структуру, проверить `magic`+`version`+`crc32` (как
   `flash_cal_load`). Невалидно → считать, что подтверждено (значения по
   умолчанию `pending_confirm=0`) — то есть на "чистой" плате фича неактивна.
2. Если `pending_confirm == 0` → ничего не делать, `boot_meta_safe_hold()`
   всегда вернёт 0.
3. Если `pending_confirm == 1`:
   - Сравнить `tracked_hash` с собственным `FW_GIT_HASH_STR` (8 байт,
     уже используется в `handle_get_version()`, ничего нового не считать).
   - Если **не совпадает** (включая случай, когда `tracked_hash` — нули,
     как сразу после `boot_meta_arm_pending()`) — это первый boot образа,
     на который распространяется данный pending: записать
     `tracked_hash = FW_GIT_HASH_STR`, `boot_attempts = 1`.
   - Если **совпадает** — это повторный boot того же образа: `boot_attempts++`.
   - В обоих случаях — записать обратно во flash (erase+program страницы).
   - если `boot_attempts >= max_attempts` → `safe_hold = 1`.

   Зачем сравнение по hash, а не просто `boot_attempts++` — см. решение по
   вопросу 4 в §1.8: это закрывает конкретный edge-case из §0.1-стиля
   инцидентов (стрелка "arm pending → рефлеш образом без `boot_meta` →
   позже рефлеш `boot_meta`-совместимым образом" не даёт унаследовать
   чужой счётчик попыток).

Логика `boot_meta_process()` (авто-confirm, вызывается каждую итерацию
`app_step()`, но реально делает что-то один раз за boot):

- Если `pending_confirm == 0` или уже подтверждено в этом boot — выход.
- Если `systick_ms >= BOOT_META_CONFIRM_STABLE_MS` — вызвать
  `boot_meta_confirm()` и выставить внутренний RAM-флаг "уже подтверждено",
  чтобы не писать во flash на каждой итерации.
- **Открытый вопрос (см. §1.8):** гейтить ли авто-confirm по
  `fault_get_flags() == 0`. По умолчанию план **не** гейтит — подтверждение
  привязано к самому факту "прошивка непрерывно живёт и крутит `app_step()`
  N секунд", а не к электрическим условиям (это епархия fault_manager и не
  имеет отношения к качеству образа).

### 1.6 Точки интеграции

`Services/app.c`:

```c
void app_init(void)
{
    power_safe_state();

    adc_service_init();
    input_service_init();
    power_manager_init();
    fault_manager_init();
    flash_cal_load();
    boot_meta_init();              /* NEW */

    if (HAL_ADCEx_Calibration_Start(&hadc) != HAL_OK) { ... }
    if (HAL_ADC_Start_DMA(...) != HAL_OK) { ... }
    if (HAL_TIM_PWM_Start(...) != HAL_OK) { ... }

    uart_protocol_init();

    if (boot_meta_safe_hold()) {
        fault_set_flag(FAULT_BOOT_UNCONFIRMED);   /* NEW: Q7 видит причину в GET_STATUS */
    } else {
        power_startup_begin();                     /* без изменений, просто под условием */
    }
}

void app_step(void)
{
    uart_protocol_process();
    adc_service_process();
    input_service_process();
    power_manager_process();
    fault_manager_process();
    bootloader_process();
    boot_meta_process();            /* NEW — порядок важен для test_main_loop_order.c, см. §1.7 */
}
```

`Services/bootloader.c` — арминг метки перед прыжком в ROM bootloader:

```c
void bootloader_process(void)
{
    if (!boot_pending) return;
    if (uart_tx_busy()) return;

    boot_meta_arm_pending();   /* NEW: пишет pending_confirm=1, boot_attempts=0, max_attempts=...,
                                   tracked_hash=0 — hash своего образа не пишет: arm_pending вызывает
                                   СТАРЫЙ образ и не знает hash того, что сейчас зальют (см. §1.8 п.4) */
    boot_magic = SRAM_MAGIC_VALUE;
    __DSB();
    boot_pending = 0;
    NVIC_SystemReset();
}
```

Примечание: `BOOTLOADER_ENTER` — не только про OTA (есть ещё
`ota_dump.sh`/чтение flash), поэтому pending будет ставиться и на чтение
тоже. Это безопасно по построению (fail-safe в консервативную сторону): если
образ не менялся, он и так пройдёт `BOOT_META_CONFIRM_STABLE_MS` без
проблем и тихо подтвердится сам на первом жеboot после возврата из
bootloader.

`Protocol/uart_protocol.c` — `RESET_FAULT` становится точкой явного,
немедленного подтверждения (не нужно ждать 10 секунд):

```c
static void handle_reset_fault(void)
{
    if (fault_get_flags() & FAULT_BOOT_UNCONFIRMED) {
        boot_meta_confirm();     /* NEW */
    }
    fault_clear_flags();
    uart_send_ack(CMD_RESET_FAULT, 0);
}
```

Это укладывается в уже задокументированный в README сценарий
"Восстановление после fault" (`GET_STATUS → RESET_FAULT → POWER_CTRL`) без
единой новой команды в протоколе — Q7 ничего не обязан знать про фичу,
existing recovery flow просто начинает "просто работать" и для этого случая.

### 1.7 Что нужно поправить в тестах/контракте

- `Tests/test_invariants.c:138,152-153` — сейчас жёстко проверяет
  `FAULT_RESERVED` как всегда-ноль. Нужно осознанно заменить на:
  `FAULT_BOOT_UNCONFIRMED` выставляется **только** когда
  `boot_meta_safe_hold() == 1`, и остаётся 0 во всех остальных сценариях.
- `Tests/test_main_loop_order.c` — сейчас фиксирует порядок вызовов внутри
  `app_step()`. Добавление `boot_meta_process()` — нужно явно прописать его
  место в ожидаемой последовательности (после `bootloader_process()`,
  как в примере выше).
- `Tests/contract_check.py` — по аналогии с проверками `HAL_ADC_Start_DMA`/
  `IWDG` добавить проверку: `boot_meta_arm_pending()` вызывается ровно один
  раз, и именно в `bootloader_process()` до `NVIC_SystemReset()`.
- Новый `Tests/test_boot_meta.c` (по образцу `Tests/test_flash_cal.c`,
  с RAM-буфером вместо реального flash — `flash_cal.c` уже параметризован
  через `FLASH_CAL_ADDR` переопределение в host-тестах, тот же трюк для
  `FLASH_BOOT_META_ADDR`):
  - валидный/невалидный magic/crc → поведение по умолчанию;
  - `boot_attempts` доходит до `max_attempts` → `boot_meta_safe_hold()==1`;
  - `boot_meta_confirm()` сбрасывает `pending_confirm` и `boot_attempts`;
  - авто-confirm по `systick_ms` в `boot_meta_process()` не пишет flash
    повторно после первого подтверждения (мок на `HAL_FLASH_Program` вызывается
    один раз);
  - **регрессия на инцидент из §0.1:** при `boot_attempts >= max_attempts`
    (`boot_meta_safe_hold()==1`) ни `boot_meta_init()`, ни `app_init()` не
    вызывают `bootloader_schedule()`/`NVIC_SystemReset()` — мок на них должен
    остаться незатронутым; отдельно проверить, что `boot_meta_confirm()` и
    `boot_meta_safe_hold()` никак не читают/не вычисляют CRC самого образа
    (`.text`/`.fw_crc`), только содержимое `boot_meta_t`.
  - **тест на решение по §1.8 п.4:** `pending_confirm=1`, `boot_attempts=2`
    (почти на лимите), `tracked_hash` = произвольный чужой hash (не равный
    hash текущей сборки в тесте) → после `boot_meta_init()` ожидается
    `boot_attempts==1` (счётчик сброшен как "новый образ"), а не `3`
    (что дало бы немедленный `safe_hold`); повторный вызов
    `boot_meta_init()` без изменения `tracked_hash` между вызовами (эмуляция
    повторного boot того же образа) → `boot_attempts==2`.
- `Tests_UART_All/` — 2 новых ручных/полуавтоматических сценария:
  - `34_ota_confirm_reset_fault.sh` — reflash → сразу `RESET_FAULT` → следующий
    reboot не считает попытку;
  - `35_ota_unconfirmed_safe_hold.sh` — reflash заведомо "плохим" (или
    искусственно — power-cycle 3 раза подряд до авто-confirm) → на 3-м boot
    `GET_STATUS.fault_flags & 0x8000 != 0`, домены не поднимаются
    автоматически, `PING`/`GET_STATUS`/`BOOTLOADER_ENTER` отвечают.
- `README.md` / `Rules_POWER.md` — задокументировать поведение
  (`Rules_POWER.md` — по явной команде пользователя, но новый инвариант
  туда логично добавить, раз он становится таким же "жёстким", как остальные
  46-53).

### 1.8 Открытые вопросы — решения

1. **Гейтить ли авто-confirm по `fault_get_flags()==0`?**

   **Решение: нет, не гейтим.** Оставляем поведение по умолчанию из §1.5.
   Подтверждение образа — это вопрос "прошивка живая и непрерывно крутит
   `app_step()`", а не "электрически всё идеально". Если гейтить по faults,
   плата с любым перманентным аппаратным дефектом (отвалившийся NTC,
   постоянно вне порога канал) никогда не подтвердит даже полностью
   исправную новую прошивку и после `BOOT_META_MAX_ATTEMPTS` уйдёт в
   safe-hold по причине, не имеющей отношения к качеству образа — это
   строго хуже, чем ложное подтверждение на фоне fault (fault всё равно
   latched и виден в `GET_STATUS` независимо от `boot_meta`, а явное
   восстановление через `RESET_FAULT` — см. п.3 — тоже подтверждает образ,
   так что "тихого" подтверждения без осведомлённости Q7 не возникает).
   В `Tests/test_boot_meta.c` (§1.7) явно зафиксировать тест: авто-confirm
   по таймеру срабатывает даже при непустом `fault_get_flags()`.

2. **`BOOT_META_MAX_ATTEMPTS=3` и `BOOT_META_CONFIRM_STABLE_MS=10000`.**

   **Решение: оставляем эти значения как дефолт, без изменений в плане.**
   Обоснование: по `README.md` (§ "Рекомендуемый цикл опроса") Q7 опрашивает
   `GET_STATUS` на 1–10 Гц, то есть за 10 секунд стабильной работы успевает
   пройти от 10 до 100 успешных опросов — этого достаточно как признак
   "образ живой", не полагаясь на BOR/ребут-статистику, которая в проекте
   не задокументирована и не может быть измерена без стенда. 3 попытки ×
   10 с = 30 с максимального времени до safe-hold, что не создаёт долгого
   зависания в pending-состоянии, но и не даёт слишком быстро списать
   образ как плохой на фоне единичного power blip. Оба параметра — это
   `#define` в `config.h`, а не архитектурное решение: если в поле
   вылезет ложный safe-hold (например, Q7 сам штатно ребутит плату чаще,
   чем раз в 30 с) — это правится одной строкой без изменения дизайна,
   поэтому не блокирует реализацию. Уточнение цифр по факту стендовых
   прогонов `Tests_UART_All/35_ota_unconfirmed_safe_hold.sh` — уже
   включить в шаг 7 плана (§3), отдельного решения сейчас не требуется.

3. **Нужен ли отдельный `CMD_CONFIRM_BOOT`?**

   **Решение: нет, не добавляем в MVP.** `RESET_FAULT` уже даёт explicit,
   немедленный confirm (§1.6), а авто-confirm по таймеру закрывает случай
   Q7-софта, который про фичу не знает — вместе это покрывает оба режима
   использования без новой команды в `contract/protocol.yaml`. Отдельная
   команда добавляет протокольную поверхность (новый код команды, новую
   строку в контракте, новый тест) ради выигрыша, который уже даёт
   существующий recovery flow. Единственный нюанс, который нужно
   зафиксировать в README (см. §1.7, пункт про документацию): `RESET_FAULT`
   очищает **весь** `fault_flags`, а не только `FAULT_BOOT_UNCONFIRMED` —
   это уже существующее поведение (инвариант 21 в `Rules_POWER.md`), не
   новый побочный эффект, но Q7 должен явно знать, что вызов `RESET_FAULT`
   сразу после OTA как "confirm" одновременно снимет и любой другой
   latched fault, если он на тот момент есть. Если это когда-то станет
   проблемой на практике — `CMD_CONFIRM_BOOT` можно добавить позже как
   отдельную фичу без пересмотра остального дизайна (confirm-механизм
   в §1.5/1.6 к конкретной команде не привязан).

4. **Пограничный случай с рефлешем образом без `boot_meta`.**

   **Решение: да, закрываем — добавлен `tracked_hash[8]` в `boot_meta_t`.**
   Цена решения минимальна (8 байт в 1 КБ странице, несколько строк
   сравнения в `boot_meta_init()`, hash уже считается на этапе сборки для
   `GET_VERSION` — `FW_GIT_HASH_STR`, никакой новой инфраструктуры), а
   закрывает конкретный, явно описанный в этом же документе (§0.1) класс
   проблем: "счётчик попыток относится не к тому образу, который сейчас
   реально запущен". Обновлённые структура/логика — см. §1.4 и §1.5 выше,
   тест — см. §1.7. Это не нарушает "не усложнять без необходимости":
   необходимость здесь прямо продемонстрирована инцидентом `17d961e0`
   (§0.1), где именно рассинхронизация состояния с реальным образом и была
   корнем проблемы.

---

## 2. Feature 2 — персистентность `SET_THRESHOLDS`

### 2.1 Проблема

`fault_set_threshold()` (`Services/fault_manager.c`) пишет только в RAM.
После каждого power cycle пороги откатываются на дефолты из `config.h`
(`THRESH_V24_MIN` и т.д.) — Q7 обязан присылать `SET_THRESHOLDS` заново на
каждом старте, иначе тихо работают дефолты.

### 2.2 Дизайн

Отдельная flash-страница (не совмещаем с `FLASH_BOOT_META` — разная частота
записи: boot_meta пишется почти на каждом OTA/boot-цикле, thresholds — редко,
по явному запросу; общая страница means один erase стирает оба, что лишний
риск и лишняя сложность read-modify-write ради экономии 1 КБ из 64):

```
FLASH          (rx) : ORIGIN = 0x08000000, LENGTH = 60K   /* было 61K после §1, 63K изначально */
FLASH_BOOT_META(r)  : ORIGIN = 0x0800F400, LENGTH = 1K
FLASH_THRESH   (r)  : ORIGIN = 0x0800F800, LENGTH = 1K    /* новое */
FLASH_CAL      (r)  : ORIGIN = 0x0800FC00, LENGTH = 1K
```

Структура (те же 4 напряжения + 5 токов, что и в `fault_manager.c`):

```c
/* Services/flash_thresh.h */
typedef struct {
    uint32_t magic;             /* 0..3 */
    uint16_t version;           /* 4..5 */
    uint16_t reserved;          /* 6..7 */
    uint16_t v_thresh_min[4];   /* 8..15  v24,v12,v5,v3v3 — тот же порядок, что в fault_manager.c */
    uint16_t v_thresh_max[4];   /* 16..23 */
    uint16_t i_thresh_max[5];   /* 24..33 lcd,bl,scaler,audio_l,audio_r */
    uint16_t reserved2;         /* 34..35 паддинг под crc32 */
    uint32_t crc32;             /* 36..39 */
} flash_thresh_t;               /* 40 байт */
```

Новая команда (по прецеденту `CALIBRATE_OFFSET` — "сними RAM-состояние и
запиши во flash" уже оформлено как отдельная команда, а не флаг в другой
команде — держим тот же стиль, а не прячем "save" в неиспользуемый бит маски
`SET_THRESHOLDS`):

```c
/* Config/config.h */
#define CMD_SAVE_THRESHOLDS   0x0BU   /* req_len=0, resp_len=1 (status) */
```

`contract/protocol.yaml`:

```yaml
- {
    name: SAVE_THRESHOLDS,
    code: 0x0B,
    req_len: 0,
    resp_len: 1,
    resp_notes: "status=0x00 OK, персистентно сохраняет текущие RAM-пороги во flash",
  }
```

API `Services/fault_manager.h` — расширить:

```c
void    fault_thresh_load(void);  /* вызывается из app_init() после fault_manager_init() */
uint8_t fault_thresh_save(void);  /* снимок текущих v_thresh_*/i_thresh_max -> flash, вызывается по CMD_SAVE_THRESHOLDS */
```

Реализация `fault_thresh_load/save` — прямая калька с `flash_cal_load/calibrate`
(включая `sw_crc32`, unlock/erase/program/lock), либо переиспользовать ту же
`sw_crc32()` — стоит вынести её в общий `Services/flash_util.c`, раз она
понадобится уже в трёх местах (`flash_cal.c`, `boot_meta.c`, `flash_thresh.c`)
буквально с одинаковым телом функции — единственное реальное
рефакторинг-решение в этом плане, не считая новых модулей.

### 2.3 Точки интеграции

`Services/app.c`:

```c
fault_manager_init();
flash_cal_load();
boot_meta_init();
fault_thresh_load();   /* NEW — переопределяет дефолты из fault_manager_init(), если flash валиден */
```

`Protocol/uart_protocol.c`:

```c
static void handle_save_thresholds(void)
{
    uint8_t result = fault_thresh_save();
    uart_send_ack(CMD_SAVE_THRESHOLDS, result);
}
...
case CMD_SAVE_THRESHOLDS: handle_save_thresholds(); break;
```

В отличие от `CALIBRATE_OFFSET` — предусловий на состояние доменов не нужно
(сохранение порогов не трогает "железо" прямо сейчас, только персистентность
текущего RAM-состояния), значит `fault_thresh_save()` можно звать в любой
момент.

### 2.4 Тесты

- `Tests/test_flash_thresh.c` (по образцу `Tests/test_flash_cal.c`): невалидный
  flash → дефолты из `config.h`; валидный → переопределяет; save/load
  round-trip; повреждённый CRC → игнор.
- `Tests/test_protocol_parser.c` / новый тест на `CMD_SAVE_THRESHOLDS`:
  `req_len` должен быть 0 (в отличие от `SET_THRESHOLDS`), NACK при лишних
  байтах.
- `Tests/contract_check.py` — сверка нового кода команды с
  `contract/protocol.yaml` (механизм уже есть для остальных команд).
- `Tests_UART_All/36_save_thresholds_roundtrip.sh` — `SET_THRESHOLDS` →
  `SAVE_THRESHOLDS` → power-cycle платы (или `RESET_BRIDGE`-аналог, вручную) →
  `GET_STATUS`/повторный `SET_THRESHOLDS` с теми же значениями показывает,
  что применённые пороги пережили reset.

### 2.5 Открытые вопросы — решения

1. **Нужна ли команда "сбросить пороги на заводские"?**

   **Решение: нет, отдельная команда не нужна.** Дефолтные значения порогов
   (`THRESH_V24_MIN` и т.д.) — константы в `config.h`, известные и Q7, и
   прошивке на этапе компиляции; ничто не мешает Q7 при необходимости
   "фабричного сброса" явно прислать `SET_THRESHOLDS` с этими самыми
   дефолтными значениями, а затем `SAVE_THRESHOLDS` — результат идентичен
   стиранию `FLASH_THRESH` (текущее RAM-состояние = дефолты, персистентно
   сохранено), но без новой команды и нового кода в протоколе. Отдельная
   команда "erase" добавила бы второй путь достижения того же результата
   ради экономии одного пакета на стороне Q7 — не оправдано (см. правило
   "не добавлять команды/слои без явной необходимости").

2. **Нужно ли ограничивать частоту `SAVE_THRESHOLDS`?**

   **Решение: нет, без ограничений на стороне прошивки.** Ресурс
   перезаписи flash на STM32F0 — порядка десятков тысяч циклов на
   страницу; команда вызывается явно по инициативе Q7 (типично один раз
   при commissioning/калибровке, не в цикле опроса), так что реального
   риска исчерпать ресурс в штатной эксплуатации нет. Вводить в прошивке
   счётчик/таймер-ограничитель — дополнительное состояние и сложность без
   чёткой необходимости (тот же принцип, что уже применён к
   `CALIBRATE_OFFSET`). Достаточно зафиксировать в `resp_notes`
   `contract/protocol.yaml` и в README текстом "не вызывать в цикле опроса,
   команда предназначена для редких, инициированных Q7 операций" — так же,
   как уже сделано для `CALIBRATE_OFFSET`.

---

## 3. Порядок реализации (рекомендуемый)

1. Замерить текущий `.text`/`.rodata` размер прошивки — подтвердить, что
   −3 КБ от `FLASH` (61K → 60K после обеих фич) не критично.
2. `flash_util.c` — вынести `sw_crc32()` из `flash_cal.c` в общий модуль
   (чисто рефакторинг, без изменения поведения; сначала — чтобы дальше на
   него опирались новые модули).
3. Threshold persistence (Feature 2) — она проще и не трогает
   `power_manager`/`bootloader`: `flash_thresh.c/h`, интеграция в
   `fault_manager.h`/`app.c`/`uart_protocol.c`/`contract/protocol.yaml`,
   тесты.
4. Линкер: добавить `FLASH_THRESH`, затем `FLASH_BOOT_META`, поправить
   `ASSERT`ы, пересобрать, убедиться что `make` и host-тесты по-прежнему
   зелёные.
5. OTA pending-confirm (Feature 1) — `boot_meta.c/h`, интеграция в
   `app.c`/`bootloader.c`/`uart_protocol.c` (`handle_reset_fault`), правка
   `FAULT_RESERVED` → `FAULT_BOOT_UNCONFIRMED` в `test_invariants.c`.
6. Обновить `README.md` (раздел OTA) и, отдельным явным шагом с
   подтверждением пользователя, `Rules_POWER.md` — новые инварианты за
   номерами 54+.
7. `Tests_UART_All/` — новые сценарии 34-36, прогнать на стенде.
