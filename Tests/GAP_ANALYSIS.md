## GAP analysis для host-тестов (`Tests/`)

Цель: зафиксировать, что именно **пока не проверяется** тестами/контракт-скриптами, и какие проверки уже добавлены, чтобы закрыть критичные инварианты.

### Уже было в проекте (до этого изменения)

- **Инварианты протокола/ADC/таймингов**: `Tests/test_invariants.c` (сверка констант с контрактом).
- **Безопасное состояние**: `Tests/test_safe_state.c` (Rules_POWER.md §3.2, инварианты safe state).
- **FSM sequencing**: `Tests/test_power_sequence.c`, `Tests/test_audio_sequence.c`, `Tests/test_startup_sm.c` (пошаговое тикание, проверка GPIO-логов).
- **Fault engine**: `Tests/test_fault_policy.c` (latched, подтверждение 5 подряд, safe state first).
- **UART parser/dispatch**: `Tests/test_protocol_parser.c` (frame layout, CRC, очередь пакетов).
- **Контракт-скрипт**: `Tests/contract_check.py` (сверка YAML contract ↔ `Config/config.h`, запрет `HAL_Delay`, уникальность `HAL_IWDG_Refresh`).

### Gap (критичное), что не хватало

- **Порядок `while(1)`** (POWER_Controller.md §0.2 / Rules_POWER.md инварианты #48–#50):
  - нужно гарантировать, что `HAL_IWDG_Refresh()` вызывается **после** `uart/adc/input/power/fault` и **в одном месте**.
  - ранее проверялась только уникальность `HAL_IWDG_Refresh()` и что он внутри `while(1)`, но **не проверялся строгий порядок** относительно остальных шагов.

- **Параметры старта ADC DMA из `main.c`** (Rules_POWER.md инварианты #30–#33):
  - `HAL_ADC_Start_DMA(..., adc_get_dma_buf(), ADC_CHANNEL_COUNT)` должен вызываться ровно один раз и с правильными аргументами.
  - ранее это не проверялось автоматически.

### Закрыто этим изменением

- **Строгая проверка порядка шагов main loop** (Rules_POWER.md #48–#50):
  - добавлено в `Tests/contract_check.py`: проверяется, что `HAL_IWDG_Refresh()` находится в `while(1)` и вызывается **после** шага приложения (`app_step()`), а также что refresh встречается **ровно в одном файле** (`Core/Src/main.c`).
  - добавлен unit-тест `Tests/test_main_loop_order.c`: проверяет фактический порядок вызовов внутри `app_step()`:
    `uart_protocol_process -> adc_service_process -> input_service_process -> power_manager_process -> fault_manager_process -> bootloader_process`,
    и что `HAL_IWDG_Refresh()` **не** вызывается из `app_step()`.

- **Проверка контракта `HAL_ADC_Start_DMA` в `main.c`**:
  - добавлено в `Tests/contract_check.py`: ровно один вызов `HAL_ADC_Start_DMA(...)` в сумме по `main.c` + `app.c`,
    и в аргументах присутствуют `adc_get_dma_buf()` и `ADC_CHANNEL_COUNT` (Rules_POWER.md #30–#33).
  - дополнительно проверяется порядок init: `HAL_ADCEx_Calibration_Start()` должен происходить **до** `HAL_ADC_Start_DMA()`.

- **HAL error-handling для критичных init-вызовов** (coding standard + contract):
  - зафиксировано в `Tests/contract_check.py`: `HAL_ADCEx_Calibration_Start`, `HAL_ADC_Start_DMA`, `HAL_TIM_PWM_Start`
    должны быть обёрнуты в `if (... != HAL_OK) { Error_Handler(); }` (разрешено в `main.c` или `app.c`).

### Gap (не критично/следующий этап), что ещё можно добавить

- **Негативные сценарии UART “ошибка/занятость”**:
  - базовые правила уже покрыты: `Tests/test_uart_hal_errors.c` проверяет, что ошибки `HAL_UART_Receive_IT`/`HAL_UART_Transmit_IT`
    эскалируются в `Error_Handler()`, а `TX busy` подавляет повторный вызов transmit.
  - если потребуется — добавить сценарии “ошибка/занятость” на уровне протокола/очереди (поведение конечного автомата TX/RX),
    а не только thin HAL-обвязку.

- **Негативные тесты на отказ периферии**:
  - сценарии “UART TX busy”, “ошибка transmit/receive”, “ADC DMA не стартовал” и т.п.
  - их удобно делать на уровне thin-wrapper/адаптеров (если появятся), либо через отдельные host-юнит-тесты, но без CMock это будет ручная трассировка/контроль кодов возврата.

