# Makefile for the stack VM
.DEFAULT_GOAL := help

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra
LDFLAGS ?=

SRC_DIR     = src
INCLUDE_DIR = include
BUILD_DIR   = build
TEST_DIR    = tests
APPS_DIR    = apps
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

SINGLE_TOOL_NAMES = vm asm dis rom
TOOL_NAMES = $(SINGLE_TOOL_NAMES) db
TOOLS      = $(foreach tool,$(TOOL_NAMES),$(BUILD_DIR)/0x$(tool))
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

LIB_STATIC = $(BUILD_DIR)/lib0xvm.a
LIB_SHARED = $(BUILD_DIR)/lib0xvm.so
CONFORMANCE_ROM = $(BUILD_DIR)/conformance-v2.rom
SNAKE_ROM = $(BUILD_DIR)/snake.rom
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

CFLAGS += -I$(INCLUDE_DIR) -I$(APPS_DIR) -I$(APPS_DIR)/common -I$(DEPS_DIR) -I$(TEST_DIR)
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
all: libs $(TOOLS) $(SDL_HOST) $(TEST_BIN) $(CONFORMANCE_ROM) ## Build libraries, tools, hosts, tests, and conformance ROM

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
$(SDL_HOST): $(APPS_DIR)/vm-sdl/main.c $(COMMON_OBJ) $(LIB_STATIC) | $(BUILD_DIR)
	@echo "[INFO] Building SDL3 host"
	$(CC) $(CFLAGS) -c $< -o $(BUILD_DIR)/app_0xvm-sdl.o
	$(CC) $(BUILD_DIR)/app_0xvm-sdl.o $(COMMON_OBJ) $(LIB_STATIC) -o $@ $(LDFLAGS)
endif

$(LIB_STATIC): $(OBJ) | $(BUILD_DIR)
	@echo "[INFO] Building lib0xvm (static)"
	ar rcs $@ $(OBJ)

$(LIB_SHARED): $(OBJ) | $(BUILD_DIR)
	@echo "[INFO] Building lib0xvm (shared)"
	$(CC) $(SHARED_FLAG) -o $@ $(OBJ) $(LDFLAGS)

.PRECIOUS: $(OBJ) $(TEST_OBJ) $(COMMON_OBJ) $(DB_OBJ)

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
	$(patsubst %,$(BUILD_DIR)/app_0x%.d,$(SINGLE_TOOL_NAMES)) $(DB_DEP) \
	$(if $(SDL_HOST),$(BUILD_DIR)/app_0xvm-sdl.d)

$(CONFORMANCE_ROM): $(TEST_DIR)/conformance.asm $(STDLIB_SRC) $(BUILD_DIR)/0xasm | $(BUILD_DIR)
	@echo "[INFO] Building conformance ROM"
	@./$(BUILD_DIR)/0xasm -f $< -o $@

.PHONY: conformance
conformance: $(CONFORMANCE_ROM) ## Build the executable v2 conformance ROM

$(SNAKE_ROM): examples/snake.asm $(STDLIB_SRC) $(BUILD_DIR)/0xasm | $(BUILD_DIR)
	@echo "[INFO] Building Snake ROM"
	@./$(BUILD_DIR)/0xasm -f $< -o $@

.PHONY: snake-check
snake-check: $(SNAKE_ROM) ## Assemble the flagship Snake app
	@echo "[RUN] Snake assembly check"

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
	@$(MAKE) --no-print-directory debugger-check
	@$(MAKE) --no-print-directory snake-check
	@$(MAKE) --no-print-directory sdl-check

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
