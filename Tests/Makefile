# ============================================================
# Host unit tests for POWER_Controller
# Requires: gcc (host), wget/curl (for Unity download)
# Usage:
#   make          — build and run all tests
#   make test_crc — build and run single suite
#   make clean    — remove build artifacts
# ============================================================

CC       = gcc
CFLAGS   = -std=c11 -Wall -Wextra -Wpedantic -g -O0 \
           -DUNIT_TEST \
           -ITests/stubs -I. -IConfig -IServices -IProtocol
LDFLAGS  =

# Unity framework
UNITY_DIR  = Tests/unity
UNITY_SRC  = $(UNITY_DIR)/unity.c
UNITY_URL  = https://raw.githubusercontent.com/ThrowTheSwitch/Unity/master/src

BUILD_DIR  = Tests/build
STUBS_SRC  = Tests/stubs/hal_stubs.c

# All test executables
TESTS = test_crc test_adc_conversion test_protocol_parser \
        test_power_ctrl test_fault_policy test_input_packing \
        test_invariants

TEST_BINS = $(addprefix $(BUILD_DIR)/, $(TESTS))

.PHONY: all clean unity_fetch $(TESTS)

all: unity_fetch $(TEST_BINS)
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
$(BUILD_DIR)/test_crc: Tests/test_crc.c $(STUBS_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_adc_conversion: Tests/test_adc_conversion.c $(STUBS_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_protocol_parser: Tests/test_protocol_parser.c $(STUBS_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_power_ctrl: Tests/test_power_ctrl.c $(STUBS_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_fault_policy: Tests/test_fault_policy.c $(STUBS_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_input_packing: Tests/test_input_packing.c $(STUBS_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

$(BUILD_DIR)/test_invariants: Tests/test_invariants.c $(STUBS_SRC) $(UNITY_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(UNITY_DIR) -o $@ $^

clean:
	rm -rf $(BUILD_DIR)
