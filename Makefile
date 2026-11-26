# Makefile for the stack VM
.DEFAULT_GOAL := help

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra
LDFLAGS ?= -lm

SRC_DIR     = src
INCLUDE_DIR = include
BUILD_DIR   = build
TEST_DIR    = tests
APPS_DIR    = apps
HOSTS_DIR   = hosts
DEPS_DIR    = deps
STDLIB_DIR  = stdlib
STDLIB_SRC  = $(wildcard $(STDLIB_DIR)/*.asm)

TARGET  = vm
VERSION = 0.1.0

SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC))
DEP = $(OBJ:.o=.d)

TEST_SRC = $(wildcard $(TEST_DIR)/*.c)
TEST_OBJ = $(patsubst $(TEST_DIR)/%.c,$(BUILD_DIR)/test_%.o,$(TEST_SRC))
TEST_DEP = $(TEST_OBJ:.o=.d)
TEST_MAIN = $(BUILD_DIR)/app_0xtest.o
TEST_BIN  = $(BUILD_DIR)/0xtest

CLI_HOST_SRC      = $(HOSTS_DIR)/cli/main.c
CLI_HOST          = $(BUILD_DIR)/0xvm
SINGLE_TOOL_NAMES = asm dis rom
TOOL_NAMES        = vm $(SINGLE_TOOL_NAMES) db
TOOLS             = $(foreach tool,$(TOOL_NAMES),$(BUILD_DIR)/0x$(tool))
COMMON_OBJ = $(BUILD_DIR)/tools_common.o $(BUILD_DIR)/host.o

DB_DIR  = $(APPS_DIR)/db
DB_SRC  = $(wildcard $(DB_DIR)/*.c)
DB_OBJ  = $(patsubst $(DB_DIR)/%.c,$(BUILD_DIR)/app_db_%.o,$(DB_SRC))
DB_DEP  = $(DB_OBJ:.o=.d)

SDL3_FOUND := $(shell pkg-config --exists sdl3 2>/dev/null && echo yes)
ifeq ($(SDL3_FOUND),yes)
    SDL3_CFLAGS := $(shell pkg-config --cflags sdl3)
    SDL3_LIBS   := $(shell pkg-config --libs sdl3)
    SDL_HOST    := $(BUILD_DIR)/0xvm-sdl
endif

EMCC       ?= emcc
NODE       ?= node
EMCC_FOUND := $(shell command -v $(EMCC) >/dev/null 2>&1 && echo yes)
NODE_FOUND := $(shell command -v $(NODE) >/dev/null 2>&1 && echo yes)
WASM_DIR   = $(HOSTS_DIR)/wasm
ifeq ($(EMCC_FOUND),yes)
    WASM_JS    := $(BUILD_DIR)/0xvm-wasm.js
    WASM_APP   := $(BUILD_DIR)/0xvm-app.js
    WASM_HTML  := $(BUILD_DIR)/0xvm-wasm.html
endif

LIB_STATIC = $(BUILD_DIR)/lib0xvm.a
LIB_SHARED = $(BUILD_DIR)/lib0xvm.so
CONFORMANCE_ROM = $(BUILD_DIR)/conformance-v2.rom
SNAKE_ROM = $(BUILD_DIR)/snake.rom
SNAKE_SMOKE_SRC = $(BUILD_DIR)/snake-smoke.asm
SNAKE_SMOKE_ROM = $(BUILD_DIR)/snake-smoke.rom
TYPOGRAPHY_ROM = $(BUILD_DIR)/typography.rom
TYPOGRAPHY_DIR = examples/typography
TYPOGRAPHY_SRC = $(wildcard $(TYPOGRAPHY_DIR)/*.asm)
TYPOGRAPHY_SMOKE_SRC = $(BUILD_DIR)/typography-smoke.asm
TYPOGRAPHY_SMOKE_ROM = $(BUILD_DIR)/typography-smoke.rom
LUAC_MODULES    = $(wildcard luac/*.py)
PYPROJECT       = pyproject.toml
PIP            ?= pip3
LUA_CASES       = smoke functions arrays heap bitops
LUA_SRCS        = $(foreach case,$(LUA_CASES),$(TEST_DIR)/lua/$(case).lua)
LUA_ASMS        = $(foreach case,$(LUA_CASES),$(BUILD_DIR)/lua-$(case).asm)
LUA_ROMS        = $(foreach case,$(LUA_CASES),$(BUILD_DIR)/lua-$(case).rom)
CONFORMANCE_EXPECTED = $(TEST_DIR)/conformance.expected
DEBUGGER_SCRIPT = $(TEST_DIR)/debugger.in
DEBUGGER_EXPECTED = $(TEST_DIR)/debugger.expected
UNAME_S   := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    SHARED_FLAG = -dynamiclib
else
    SHARED_FLAG = -shared
endif
LIBS = $(LIB_STATIC) $(LIB_SHARED)

CFLAGS += -I$(INCLUDE_DIR) -I$(APPS_DIR) -I$(APPS_DIR)/common -I$(HOSTS_DIR) -I$(DEPS_DIR) -I$(TEST_DIR)
CFLAGS += '-DASM_STDLIB_DIR="$(CURDIR)/$(STDLIB_DIR)"'
CFLAGS += -MMD -MP -fPIC
LDFLAGS +=

# Profile: `make type=RELEASE` for an optimized build
ifeq ($(type), RELEASE)
	CFLAGS += -O3 -DNDEBUG
else
	SANITIZERS = -fsanitize=address,undefined
	CFLAGS  += -DDEBUG -ggdb $(SANITIZERS)
	LDFLAGS += $(SANITIZERS)
endif

TOTAL_FILES := $(words $(SRC))

.PHONY: all
all: libs $(TOOLS) $(SDL_HOST) $(WASM_JS) $(WASM_APP) $(WASM_HTML) $(TEST_BIN) $(CONFORMANCE_ROM) $(SNAKE_ROM) $(TYPOGRAPHY_ROM) ## Build libraries, tools, hosts, tests, and ROMs

.PHONY: libs
libs: $(LIBS) ## Build static and shared lib0xvm

.PHONY: static
static: $(LIB_STATIC) ## Build static lib0xvm

.PHONY: shared
shared: $(LIB_SHARED) ## Build shared lib0xvm

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR) ## Compile a library source file
	@echo "[INFO] Compiling $<"
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_%.o: $(TEST_DIR)/%.c | $(BUILD_DIR) ## Compile a test source file
	@echo "[INFO] Compiling test $<"
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/tools_common.o: $(APPS_DIR)/common/tools.c $(APPS_DIR)/common/tools.h | $(BUILD_DIR)
	@echo "[INFO] Compiling tool helpers"
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/host.o: $(APPS_DIR)/common/host.c $(APPS_DIR)/common/host.h | $(BUILD_DIR)
	@echo "[INFO] Compiling shared host devices"
	$(CC) $(CFLAGS) -c -o $@ $<

ifneq ($(SDL_HOST),)
$(SDL_HOST): CFLAGS += $(SDL3_CFLAGS)
$(SDL_HOST): LDFLAGS += $(SDL3_LIBS)
$(SDL_HOST): $(HOSTS_DIR)/sdl3/main.c $(COMMON_OBJ) $(LIB_STATIC) | $(BUILD_DIR)
	@echo "[INFO] Building SDL3 host"
	$(CC) $(CFLAGS) -c $< -o $(BUILD_DIR)/app_0xvm-sdl.o
	$(CC) $(BUILD_DIR)/app_0xvm-sdl.o $(COMMON_OBJ) $(LIB_STATIC) -o $@ $(LDFLAGS)
endif

ifeq ($(EMCC_FOUND),yes)
$(WASM_JS): $(SRC) $(WASM_DIR)/main.c $(WASM_DIR)/library.js $(INCLUDE_DIR)/vm.h | $(BUILD_DIR)
	@echo "[INFO] Building browser/WASM host"
	$(EMCC) $(SRC) $(WASM_DIR)/main.c --js-library $(WASM_DIR)/library.js \
		-I$(INCLUDE_DIR) -O2 --no-entry \
		-sMODULARIZE=1 -sEXPORT_NAME=create0xvm \
		-sENVIRONMENT=web,worker,node -sALLOW_MEMORY_GROWTH=1 \
		-sSTACK_SIZE=65536 \
		-sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8"]' \
		-sEXPORTED_FUNCTIONS='["_vmw_init","_vmw_load","_vmw_run","_vmw_step","_vmw_status","_vmw_ticks","_vmw_ip","_vmw_sp","_vmw_peek8","_vmw_push_key","_vmw_request_quit","_vmw_console_input","_vmw_set_debug"]' \
		-o $@

$(WASM_APP): $(WASM_DIR)/app.js | $(BUILD_DIR)
	@cp $< $@

$(WASM_HTML): $(WASM_DIR)/index.html | $(BUILD_DIR)
	@cp $< $@
endif

$(LIB_STATIC): $(OBJ) | $(BUILD_DIR)
	@echo "[INFO] Building lib0xvm (static)"
	ar rcs $@ $(OBJ)

$(LIB_SHARED): $(OBJ) | $(BUILD_DIR)
	@echo "[INFO] Building lib0xvm (shared)"
	$(CC) $(SHARED_FLAG) -o $@ $(OBJ) $(LDFLAGS)

.PRECIOUS: $(OBJ) $(TEST_OBJ) $(COMMON_OBJ) $(DB_OBJ)

$(CLI_HOST): $(CLI_HOST_SRC) $(COMMON_OBJ) $(LIB_STATIC) | $(BUILD_DIR)
	@echo "[INFO] Building CLI host"
	$(CC) $(CFLAGS) -c $< -o $(BUILD_DIR)/app_0xvm.o
	$(CC) $(BUILD_DIR)/app_0xvm.o $(COMMON_OBJ) $(LIB_STATIC) -o $@ $(LDFLAGS)

$(foreach tool,$(SINGLE_TOOL_NAMES),$(BUILD_DIR)/0x$(tool)): $(BUILD_DIR)/0x%: $(APPS_DIR)/%/main.c $(COMMON_OBJ) $(LIB_STATIC) | $(BUILD_DIR)
	@echo "[INFO] Building tool '0x$*'"
	$(CC) $(CFLAGS) -c $< -o $(BUILD_DIR)/app_0x$*.o
	$(CC) $(BUILD_DIR)/app_0x$*.o $(COMMON_OBJ) $(LIB_STATIC) -o $@ $(LDFLAGS)

$(BUILD_DIR)/app_db_%.o: $(DB_DIR)/%.c $(DB_DIR)/db.h | $(BUILD_DIR)
	@echo "[INFO] Compiling $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/0xdb: $(DB_OBJ) $(COMMON_OBJ) $(LIB_STATIC) | $(BUILD_DIR)
	@echo "[INFO] Building tool '0xdb'"
	$(CC) $(DB_OBJ) $(COMMON_OBJ) $(LIB_STATIC) -o $@ $(LDFLAGS)

$(TEST_MAIN): $(APPS_DIR)/test/main.c | $(BUILD_DIR)
	@echo "[INFO] Compiling test runner"
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_BIN): $(TEST_MAIN) $(TEST_OBJ) $(COMMON_OBJ) $(LIB_STATIC) | $(BUILD_DIR)
	@echo "[INFO] Building 0xtest"
	$(CC) $(TEST_MAIN) $(TEST_OBJ) $(COMMON_OBJ) $(LIB_STATIC) -o $@ $(LDFLAGS)

-include $(DEP) $(TEST_DEP) $(TEST_MAIN:.o=.d) $(COMMON_OBJ:.o=.d) \
	$(patsubst %,$(BUILD_DIR)/app_0x%.d,vm $(SINGLE_TOOL_NAMES)) $(DB_DEP) \
	$(if $(SDL_HOST),$(BUILD_DIR)/app_0xvm-sdl.d)

$(CONFORMANCE_ROM): $(TEST_DIR)/conformance.asm $(STDLIB_SRC) $(BUILD_DIR)/0xasm | $(BUILD_DIR)
	@echo "[INFO] Building conformance ROM"
	@./$(BUILD_DIR)/0xasm -f $< -o $@

.PHONY: conformance
conformance: $(CONFORMANCE_ROM) ## Build the executable v2 conformance ROM

$(SNAKE_ROM): examples/snake.asm $(STDLIB_SRC) $(BUILD_DIR)/0xasm | $(BUILD_DIR)
	@echo "[INFO] Building Snake ROM"
	@./$(BUILD_DIR)/0xasm -f $< -o $@

$(SNAKE_SMOKE_SRC): examples/snake.asm | $(BUILD_DIR)
	@sed -e 's/^EQU FRAME_MS    80/EQU FRAME_MS    1/' \
	     -e 's/^EQU MAX_MOVES   0/EQU MAX_MOVES   12/' $< > $@

$(SNAKE_SMOKE_ROM): $(SNAKE_SMOKE_SRC) $(STDLIB_SRC) $(BUILD_DIR)/0xasm
	@./$(BUILD_DIR)/0xasm -f $< -o $@

.PHONY: snake-check
ifeq ($(SDL_HOST),)
snake-check: $(SNAKE_ROM) ## Assemble the flagship Snake app (or smoke-run with SDL3)
	@echo "[RUN] Snake assembly check (SDL3 not found)"
else
snake-check: $(SNAKE_ROM) $(SNAKE_SMOKE_ROM) $(SDL_HOST)
	@echo "[RUN] Snake SDL smoke check (dummy video)"
	@out=$$(SDL_VIDEODRIVER=dummy ./$(SDL_HOST) -f $(SNAKE_SMOKE_ROM) -W 320 -H 240 2>&1); \
		printf '%s\n' "$$out" | grep -q 'VM_HALTED' || { printf '%s\n' "$$out"; exit 1; }; \
		printf '%s\n' "$$out" | grep -q 'ERRO' && { printf '%s\n' "$$out"; exit 1; }; \
		exit 0
endif

$(TYPOGRAPHY_ROM): examples/typography.asm $(TYPOGRAPHY_SRC) $(STDLIB_SRC) $(BUILD_DIR)/0xasm | $(BUILD_DIR)
	@echo "[INFO] Building typography ROM"
	@./$(BUILD_DIR)/0xasm -f $< -o $@

# Overrides go before the includes; constants.asm honours them via IFNDEF.
$(TYPOGRAPHY_SMOKE_SRC): examples/typography.asm $(TYPOGRAPHY_SRC) | $(BUILD_DIR)
	@{ printf 'EQU FRAME_MS 1\nEQU MAX_FRAMES 3\n'; \
	   sed -e 's|INCLUDE "typography/|INCLUDE "../examples/typography/|' $<; \
	 } > $@

$(TYPOGRAPHY_SMOKE_ROM): $(TYPOGRAPHY_SMOKE_SRC) $(STDLIB_SRC) $(BUILD_DIR)/0xasm
	@./$(BUILD_DIR)/0xasm -f $< -o $@

.PHONY: typography-check
ifeq ($(SDL_HOST),)
typography-check: $(TYPOGRAPHY_ROM) ## Assemble the typography example (or smoke-run with SDL3)
	@echo "[RUN] Typography assembly check (SDL3 not found)"
else
typography-check: $(TYPOGRAPHY_ROM) $(TYPOGRAPHY_SMOKE_ROM) $(SDL_HOST)
	@echo "[RUN] Typography SDL smoke check (dummy video)"
	@out=$$(SDL_VIDEODRIVER=dummy ./$(SDL_HOST) -f $(TYPOGRAPHY_SMOKE_ROM) -W 320 -H 240 2>&1); \
		printf '%s\n' "$$out" | grep -q 'VM_HALTED' || { printf '%s\n' "$$out"; exit 1; }; \
		printf '%s\n' "$$out" | grep -q 'TYPOGRAPHY OK' || { printf '%s\n' "$$out"; exit 1; }; \
		printf '%s\n' "$$out" | grep -q 'ERRO' && { printf '%s\n' "$$out"; exit 1; }; \
		exit 0
endif

$(BUILD_DIR)/lua-%.asm: $(TEST_DIR)/lua/%.lua $(LUAC_MODULES) $(PYPROJECT) | $(BUILD_DIR)
	@echo "[INFO] Compiling Lua source $*"
	@python3 -m luac compile $< -o $@

$(BUILD_DIR)/lua-%.rom: $(BUILD_DIR)/lua-%.asm $(BUILD_DIR)/0xasm
	@echo "[INFO] Assembling Lua output $*"
	@./$(BUILD_DIR)/0xasm -f $< -o $@

.PHONY: lua-check
lua-check: $(LUA_ROMS) $(BUILD_DIR)/0xvm ## Compile and run the Lua subset golden programs
	@echo "[RUN] Lua compiler front-end unit checks"
	@python3 $(TEST_DIR)/test_luac.py
	@for case in $(LUA_CASES); do \
		echo "[RUN] Lua $$case check"; \
		./$(BUILD_DIR)/0xvm -f $(BUILD_DIR)/lua-$$case.rom 2>/dev/null \
			| sed '/^\[INFO\] /d' > $(BUILD_DIR)/lua-$$case-actual.txt; \
		diff -u $(TEST_DIR)/lua/$$case.expected $(BUILD_DIR)/lua-$$case-actual.txt || exit 1; \
	done

.PHONY: luac-install
luac-install: ## Install the 0xluac CLI in editable mode
	@echo "[INFO] Installing 0xluac"
	@$(PIP) install -e .

.PHONY: conformance-check
conformance-check: conformance $(BUILD_DIR)/0xvm ## Run the conformance ROM through the CLI host
	@echo "[RUN] CLI conformance check"
	@printf 'B-7\nhey\n' | ./$(BUILD_DIR)/0xvm -f $(CONFORMANCE_ROM) 2>/dev/null \
		| sed '/^\[INFO\] /d' > $(BUILD_DIR)/conformance-actual.txt
	@diff -u $(CONFORMANCE_EXPECTED) $(BUILD_DIR)/conformance-actual.txt

.PHONY: debugger-check
debugger-check: $(BUILD_DIR)/0xdb examples/gcd.asm $(DEBUGGER_SCRIPT) $(DEBUGGER_EXPECTED) ## Run a scripted 0xdb session
	@echo "[RUN] 0xdb command check"
	@./$(BUILD_DIR)/0xdb -f examples/gcd.asm -x $(DEBUGGER_SCRIPT) \
		> $(BUILD_DIR)/debugger-actual.txt
	@diff -u $(DEBUGGER_EXPECTED) $(BUILD_DIR)/debugger-actual.txt

.PHONY: test
test: $(TEST_BIN) $(BUILD_DIR)/0xvm ## Build and run the test app and conformance checks
	@echo "[RUN] 0xtest"
	@$(TEST_BIN)
	@$(MAKE) --no-print-directory conformance-check
	@$(MAKE) --no-print-directory lua-check
	@$(MAKE) --no-print-directory debugger-check
	@$(MAKE) --no-print-directory snake-check
	@$(MAKE) --no-print-directory typography-check
	@$(MAKE) --no-print-directory sdl-check
ifeq ($(EMCC_FOUND),yes)
	@$(MAKE) --no-print-directory wasm-check
endif

BENCH_RUNS     ?= 20
THRESHOLD      ?= 0.25
BENCH_DIR      := bench
BENCH_SCRATCH  := $(BUILD_DIR)/bench
BENCH_RESULT   := $(BENCH_SCRATCH)/last.json
BENCH_BASELINE := $(BENCH_DIR)/baseline.json
PERF_BASELINE  := $(BENCH_DIR)/perf-profile.txt
BASELINE       ?= $(BENCH_BASELINE)
CURRENT        ?= $(BENCH_RESULT)

.PHONY: perf-check
perf-check: $(BUILD_DIR)/0xvm $(CONFORMANCE_ROM) ## Check CLI timing/profile output
	@echo "[RUN] CLI profile check"
	@printf 'B-7\nhey\n' | ./$(BUILD_DIR)/0xvm -f $(CONFORMANCE_ROM) -p 2> $(BUILD_DIR)/perf-check.err > /dev/null
	@grep -q '\[profile\] total ticks=' $(BUILD_DIR)/perf-check.err
	@grep -q '\[profile\] PUSH' $(BUILD_DIR)/perf-check.err

.PHONY: perf-update
perf-update: perf-check ## Save the latest profile as the committed baseline
	@mkdir -p $(BENCH_DIR)
	@cp $(BUILD_DIR)/perf-check.err $(PERF_BASELINE)
	@echo "[INFO] Saved profile baseline $(PERF_BASELINE)"

.PHONY: perf-compare
perf-compare: perf-check ## Diff the current CLI profile against the committed baseline
	@diff -u $(PERF_BASELINE) $(BUILD_DIR)/perf-check.err

.PHONY: bench
bench: $(BUILD_DIR)/0xvm $(CONFORMANCE_ROM) scripts/bench.py examples/statistics.asm ## Run repeatable benchmarks and save JSON to build/bench/
	@mkdir -p $(BENCH_SCRATCH)
	@python3 scripts/bench.py run --runs $(BENCH_RUNS) --output $(BENCH_RESULT)

.PHONY: bench-update
bench-update: bench ## Save the latest benchmark run as the committed baseline
	@mkdir -p $(BENCH_DIR)
	@cp $(BENCH_RESULT) $(BENCH_BASELINE)
	@echo "[INFO] Saved benchmark baseline $(BENCH_BASELINE)"

.PHONY: bench-compare
bench-compare: bench ## Compare current benchmarks with the committed baseline [BASELINE=... CURRENT=... THRESHOLD=...]
	@test -f "$(BASELINE)" || { echo "Missing baseline $(BASELINE); run make bench-update"; exit 1; }
	@python3 scripts/bench.py compare "$(BASELINE)" "$(CURRENT)" --threshold $(THRESHOLD)

.PHONY: wasm
ifeq ($(EMCC_FOUND),yes)
wasm: $(WASM_JS) $(WASM_APP) $(WASM_HTML) $(SNAKE_ROM) $(TYPOGRAPHY_ROM) ## Build the browser/WASM host and example ROMs
else
wasm:
	@echo "[WARN] emcc not found; skipping browser/WASM host"
endif

.PHONY: wasm-check
ifeq ($(EMCC_FOUND),yes)
ifeq ($(NODE_FOUND),yes)
wasm-check: $(WASM_JS) $(SNAKE_SMOKE_ROM) $(CONFORMANCE_ROM) $(TYPOGRAPHY_ROM) ## Smoke-run Snake, conformance and typography in the browser host under Node
	@echo "[RUN] WASM Snake smoke check"
	@$(NODE) tests/wasm-smoke.mjs $(WASM_JS) $(SNAKE_SMOKE_ROM) \
		$(CONFORMANCE_ROM) $(CONFORMANCE_EXPECTED) $(TYPOGRAPHY_ROM)
else
wasm-check: $(WASM_JS) $(SNAKE_SMOKE_ROM) $(CONFORMANCE_ROM)
	@echo "[WARN] node not found; skipping WASM Snake smoke check"
endif
else
wasm-check:
	@echo "[WARN] emcc not found; skipping WASM Snake smoke check"
endif

.PHONY: sdl
ifeq ($(SDL_HOST),)
sdl:
	@echo "[WARN] SDL3 not found via pkg-config; skipping SDL host"
else
sdl: $(SDL_HOST) ## Build the SDL3 host
endif

.PHONY: sdl-check
ifeq ($(SDL_HOST),)
sdl-check:
	@echo "[WARN] SDL3 not found via pkg-config; skipping SDL conformance check"
else
sdl-check: conformance $(SDL_HOST) ## Run the conformance ROM through the SDL3 host
	@echo "[RUN] SDL3 conformance check (dummy video)"
	@printf 'B-7\nhey\n' | SDL_VIDEODRIVER=dummy ./$(SDL_HOST) -f $(CONFORMANCE_ROM) 2>/dev/null \
		| sed '/^\[INFO\] /d' > $(BUILD_DIR)/conformance-sdl-actual.txt
	@diff -u $(CONFORMANCE_EXPECTED) $(BUILD_DIR)/conformance-sdl-actual.txt
endif

.PHONY: clean
clean: ## Remove all build files, libraries, and tools
	rm -rf $(BUILD_DIR)

.PHONY: help
help: ## Show this help message
	@echo "Available commands:"
	@grep -h -E '^[a-zA-Z0-9_.-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-25s\033[0m %s\n", $$1, $$2}'
