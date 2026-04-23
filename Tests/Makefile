# ============================================================
# Host unit tests for POWER_Controller
# Requires: gcc (host), wget/curl (for Unity download)
# Usage:
#   make          — build and run all tests
#   make test_crc — build and run single suite
#   make clean    — remove build artifacts
# ============================================================

CC = gcc

# Make this Makefile runnable from any cwd:
# - from repo root:   make -f Tests/Makefile
# - from Tests dir:   make
THIS_MAKEFILE := $(lastword $(MAKEFILE_LIST))
TESTS_DIR     := $(patsubst %/,%,$(dir $(THIS_MAKEFILE)))
REPO_ROOT     := $(abspath $(TESTS_DIR)/..)

CFLAGS   = -std=c11 -Wall -Wextra -Wpedantic -g -O0 \
           -DUNIT_TEST \
           -I$(TESTS_DIR)/stubs -I$(TESTS_DIR) \
           -I$(REPO_ROOT) -I$(REPO_ROOT)/Config -I$(REPO_ROOT)/Services -I$(REPO_ROOT)/Protocol \
           -I$(REPO_ROOT)/Core/Inc
LDFLAGS  =

# Unity framework
UNITY_DIR  = $(TESTS_DIR)/unity
UNITY_SRC  = $(UNITY_DIR)/unity.c
UNITY_URL  = https://raw.githubusercontent.com/ThrowTheSwitch/Unity/master/src

BUILD_DIR  = $(TESTS_DIR)/build
STUBS_SRC  = $(TESTS_DIR)/stubs/hal_stubs.c
PTH_SRC    = $(TESTS_DIR)/stubs/power_test_helpers.c

# All test executables
TESTS = test_crc test_adc_conversion test_protocol_parser \
        test_power_ctrl test_fault_policy test_input_packing \
        test_invariants test_safe_state test_power_sequence \
        test_audio_sequence test_sus_s3 test_bridge_reset \
        test_flash_cal test_bootloader test_brightness \
        test_startup_sm test_main_loop_order test_uart_hal_errors \
        test_app_init_order

TEST_BINS = $(addprefix $(BUILD_DIR)/, $(TESTS))

.PHONY: all clean unity_fetch $(TESTS) contract_check

all: contract_check unity_fetch $(TEST_BINS)
	@echo "========================================="
	@echo " Running all tests"
	@echo "========================================="
	@fail=0; \
	for t in $(TEST_BINS); do \
		echo "--- $$t ---"; \
		$$t || fail=1; \
		echo ""; \
	done; \
	if [ $$fail -ne 0 ]; then echo "SOME TESTS FAILED"; exit 1; fi
	@echo "========================================="
	@echo " All test suites executed"
	@echo "========================================="

# Machine-readable contract vs code check (YAML vs config.h)
contract_check:
	@python3 $(TESTS_DIR)/contract_check.py

# Individual test targets
$(TESTS): %: $(BUILD_DIR)/%
	$(BUILD_DIR)/$@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# ---- Unity download ----
unity_fetch: $(UNITY_SRC)

$(UNITY_SRC):
	mkdir -p $(UNITY_DIR)
	wget -q -O $(UNITY_DIR)/unity.h $(UNITY_URL)/unity.h
	wget -q -O $(UNITY_DIR)/unity_internals.h $(UNITY_URL)/unity_internals.h
	wget -q -O $(UNITY_DIR)/unity.c $(UNITY_URL)/unity.c
	@echo "Unity framework downloaded."

# ---- Build rules ----
# Each test is a standalone executable: test source + stubs + unity
$(BUILD_DIR)/test_crc: $(TESTS_DIR)/test_crc.c $(STUBS_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_adc_conversion: $(TESTS_DIR)/test_adc_conversion.c $(STUBS_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_protocol_parser: $(TESTS_DIR)/test_protocol_parser.c $(STUBS_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_power_ctrl: $(TESTS_DIR)/test_power_ctrl.c $(STUBS_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_fault_policy: $(TESTS_DIR)/test_fault_policy.c $(STUBS_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_input_packing: $(TESTS_DIR)/test_input_packing.c $(STUBS_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_invariants: $(TESTS_DIR)/test_invariants.c $(STUBS_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_safe_state: $(TESTS_DIR)/test_safe_state.c $(STUBS_SRC) $(PTH_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_power_sequence: $(TESTS_DIR)/test_power_sequence.c $(STUBS_SRC) $(PTH_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_audio_sequence: $(TESTS_DIR)/test_audio_sequence.c $(STUBS_SRC) $(PTH_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_sus_s3: $(TESTS_DIR)/test_sus_s3.c $(STUBS_SRC) $(PTH_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_bridge_reset: $(TESTS_DIR)/test_bridge_reset.c $(STUBS_SRC) $(PTH_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_flash_cal: $(TESTS_DIR)/test_flash_cal.c $(STUBS_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_bootloader: $(TESTS_DIR)/test_bootloader.c $(STUBS_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Wno-int-to-pointer-cast -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_brightness: $(TESTS_DIR)/test_brightness.c $(STUBS_SRC) $(PTH_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_startup_sm: $(TESTS_DIR)/test_startup_sm.c $(STUBS_SRC) $(PTH_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_main_loop_order: $(TESTS_DIR)/test_main_loop_order.c $(STUBS_SRC) $(REPO_ROOT)/Services/app.c $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_uart_hal_errors: $(TESTS_DIR)/test_uart_hal_errors.c $(STUBS_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_app_init_order: $(TESTS_DIR)/test_app_init_order.c $(STUBS_SRC) $(REPO_ROOT)/Services/app.c $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

clean:
	rm -rf $(BUILD_DIR)
