# Munt386 -- build for the host-side emulator, the automated tests, and the
# TI-84 Plus CSE backend.
#
#   make            build the host emulator, test runner, and tools
#   make host       build host targets only
#   make test       build and run the automated test suite
#   make emulator   build ./build/munt386
#   make cse        build the CSE bare-metal image (requires SDCC)
#   make cse-sim    build+run the CSE backend host simulator (same sources)
#   make firmware   assemble the Z80 bring-up asm (requires SPASM-ng/brass/sass)
#   make clean      remove build products

CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Iinclude -g
LDFLAGS ?=

# SDCC Z80 toolchain for the CSE target.  SDCC >= 4.2 is required: 4.0.0 has a
# frontend bug (duplicate-symbol errors for declarations inside switch-case
# blocks, e.g. src/cpu.c).  --nogcse works around an SDCC 4.2.0 z80 optimizer
# internal error on the for-loop in dos_puts() (src/dos.c); it only disables
# global common-subexpression elimination, no semantic change.
SDCC    ?= sdcc
SDASZ80 ?= sdasz80
SDCFLAGS := -mz80 --no-xram --std-c99 --disable-warning 110 --nogcse
# --no-xram predates SDCC 4.2 and is ignored there (warning 117); the CSE
# image uses only CODE/HOME/DATA so it stays harmless either way.

BUILD   := build
BUILDH  := $(BUILD)/host
BUILDC  := $(BUILD)/cse

# Portable core: everything except the host front end.
LIB_SRC  := $(filter-out src/main.c,$(wildcard src/*.c))
APP_SRC  := src/main.c $(LIB_SRC)
# Core without the host-only backends (the CSE replaces platform_host.c and
# the flat memory backend with its own implementations).
CSE_CORE := $(filter-out src/platform_host.c src/memflat.c,$(LIB_SRC))
# Host test suite: tests/cse is built separately (cse-sim) because it links
# the paged memory backend instead of the flat one.
TEST_SRC := tests/test_main.c $(filter-out tests/cse/%,$(wildcard tests/*/*.c))
CSE_SRC  := $(wildcard firmware/cse/*.c)

all: $(BUILDH)/munt386 $(BUILDH)/run_tests $(BUILDH)/mkdisk

# ---------------------------------------------------------------------
# Host targets (behaviour unchanged; separate object tree from the CSE)
# ---------------------------------------------------------------------

host: $(BUILDH)/munt386 $(BUILDH)/mkdisk

$(BUILDH) $(BUILDC):
	@mkdir -p $@

$(BUILDH)/munt386: $(APP_SRC) | $(BUILDH)
	$(CC) $(CFLAGS) -o $@ $(APP_SRC) $(LDFLAGS)

$(BUILDH)/run_tests: $(TEST_SRC) $(LIB_SRC) | $(BUILDH)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRC) $(LIB_SRC) $(LDFLAGS)

$(BUILDH)/mkdisk: tools/mkdisk.c | $(BUILDH)
	$(CC) $(CFLAGS) -o $@ tools/mkdisk.c $(LDFLAGS)

emulator: $(BUILDH)/munt386
tools: $(BUILDH)/mkdisk

test: $(BUILDH)/run_tests
	@$(BUILDH)/run_tests

# ---------------------------------------------------------------------
# CSE backend host simulator (make cse-sim)
# ---------------------------------------------------------------------
# The firmware/cse backend is compiled for the HOST with MUNT386_HW_EMULATE
# (port I/O to a simulated MMIO space) and MUNT386_CSE_SIM, together with the
# portable core using the PAGED memory backend (firmware/cse/cse_mem.c), not
# the flat one.  tests/cse/cse_sim_main.c provides main(), which calls
# cse_startup() exactly as the Z80 bootstrap would.

CSE_SIM_CFLAGS := $(CFLAGS) -DMUNT386_HW_EMULATE -DMUNT386_CSE_SIM \
                  -DMUNT386_CSE -DCSE_ARENA_SIZE='(4u*1024u*1024u)' \
                  -Iinclude -Ifirmware/cse
CSE_SIM_SRC := tests/cse/cse_sim_main.c tests/cse/test_cse.c \
               $(filter-out firmware/cse/main.c,$(CSE_SRC)) $(CSE_CORE)

$(BUILDH)/cse_sim: $(CSE_SIM_SRC) | $(BUILDH)
	$(CC) $(CSE_SIM_CFLAGS) -o $@ $(CSE_SIM_SRC) $(LDFLAGS) -lpthread

cse-sim: $(BUILDH)/cse_sim
	@$(BUILDH)/cse_sim

# ---------------------------------------------------------------------
# CSE target (bare-metal, SDCC Z80)
# ---------------------------------------------------------------------
# The CSE image links ONLY firmware/cse/*.c (+ startup.s) and CSE_CORE (the
# portable core minus the host-only backends).  Host-only files are never
# linked.

.PHONY: cse
cse:
	@command -v $(SDCC) >/dev/null 2>&1 || { \
	  echo "make cse: SKIP -- SDCC not found (install sdcc, e.g. apt install sdcc)"; \
	  exit 2; }
	@mkdir -p $(BUILDC)
	$(SDASZ80) -o $(BUILDC)/cse_bootstrap.rel firmware/cse/startup.s
	@set -e; for src in firmware/cse/main.c firmware/cse/cse_main.c \
	  firmware/cse/cse_ports.c \
	  firmware/cse/cse_video.c firmware/cse/cse_keys.c \
	  firmware/cse/cse_disk.c firmware/cse/cse_mem.c \
	  firmware/cse/banking.c firmware/cse/startup.c $(CSE_CORE); do \
	  obj=$(BUILDC)/$$(basename $$src .c).rel; \
	  echo "  SDCC $$src"; \
	  $(SDCC) $(SDCFLAGS) -c -Iinclude -Ifirmware/cse \
	    -DMUNT386_CSE -DMUNT386_DEVICE -DCSE_ARENA_SIZE='(32u*1024u)' \
	    -o $$obj $$src || exit 1; \
	done
	$(SDCC) $(SDCFLAGS) --no-std-crt0 --code-loc 0x0200 --data-loc 0xC000 \
	  -o $(BUILDC)/munt386-cse.bin \
	  $(BUILDC)/main.rel $(BUILDC)/cse_main.rel $(BUILDC)/cse_ports.rel $(BUILDC)/cse_video.rel \
	  $(BUILDC)/cse_keys.rel $(BUILDC)/cse_disk.rel $(BUILDC)/cse_mem.rel \
	  $(BUILDC)/banking.rel $(BUILDC)/startup.rel \
	  $(patsubst src/%.c,$(BUILDC)/%.rel,$(CSE_CORE)) \
	  $(BUILDC)/cse_bootstrap.rel

# ---------------------------------------------------------------------
# Z80 bring-up assembly (existing firmware bring-up path)
# ---------------------------------------------------------------------

.PHONY: firmware
firmware:
	@sh ./firmware/build.sh

clean:
	@rm -rf $(BUILD)

.PHONY: all host emulator tools test cse cse-sim firmware clean
