# Munt386 -- build for the host-side emulator and automated tests.
#
#   make            build the emulator and the test runner
#   make emulator   build only the host emulator
#   make test       build and run the automated test suite
#   make clean      remove build products

CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Iinclude -g
LDFLAGS ?=

BUILD   := build
LIB_SRC := $(filter-out src/main.c,$(wildcard src/*.c))
APP_SRC := src/main.c $(LIB_SRC)
TEST_SRC := tests/test_main.c $(wildcard tests/*/*.c)

all: $(BUILD)/munt386 $(BUILD)/run_tests $(BUILD)/mkdisk

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/munt386: $(APP_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(APP_SRC) $(LDFLAGS)

$(BUILD)/run_tests: $(TEST_SRC) $(LIB_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRC) $(LIB_SRC) $(LDFLAGS)

$(BUILD)/mkdisk: tools/mkdisk.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tools/mkdisk.c $(LDFLAGS)

emulator: $(BUILD)/munt386

tools: $(BUILD)/mkdisk

test: $(BUILD)/run_tests
	@$(BUILD)/run_tests

clean:
	@rm -rf $(BUILD)

.PHONY: all emulator tools test clean
