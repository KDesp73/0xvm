# SPDX-License-Identifier: MIT
# Shared paths, tool detection, flags, and ROM locations.
# Included by the top-level Makefile.

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
BOOT_ROM = $(BUILD_DIR)/boot.rom
BOOT_SRC = examples/boot.asm
BOOT_SMOKE_SRC = $(BUILD_DIR)/boot-smoke.asm
BOOT_SMOKE_ROM = $(BUILD_DIR)/boot-smoke.rom
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
LUA_CASES       = smoke functions arrays heap bitops data strings modules control devices
LUA_SRCS        = $(foreach case,$(LUA_CASES),$(TEST_DIR)/lua/$(case).lua)
LUA_MODULE_SRCS = $(wildcard $(TEST_DIR)/lua/modules/*.lua)
LUA_ASMS        = $(foreach case,$(LUA_CASES),$(BUILD_DIR)/lua-$(case).asm)
LUA_MAPS        = $(foreach case,$(LUA_CASES),$(BUILD_DIR)/lua-$(case).lumap)
.SECONDARY: $(LUA_ASMS)
LUA_ROMS        = $(foreach case,$(LUA_CASES),$(BUILD_DIR)/lua-$(case).rom)
LUA_DEVICE_SMOKE_SRC = $(BUILD_DIR)/lua-devices-smoke.lua
LUA_DEVICE_SMOKE_ROM = $(BUILD_DIR)/lua-devices-smoke.rom
LUA_TYPOGRAPHY_SRC = examples/typography.lua
LUA_TYPOGRAPHY_ASM = $(BUILD_DIR)/lua-typography.asm
LUA_TYPOGRAPHY_ROM = $(BUILD_DIR)/lua-typography.rom
LUA_TYPOGRAPHY_CLI_SRC = $(BUILD_DIR)/lua-typography-cli.lua
LUA_TYPOGRAPHY_CLI_ASM = $(BUILD_DIR)/lua-typography-cli.asm
LUA_TYPOGRAPHY_CLI_ROM = $(BUILD_DIR)/lua-typography-cli.rom
LUA_TYPOGRAPHY_SMOKE_SRC = $(BUILD_DIR)/lua-typography-smoke.lua
LUA_TYPOGRAPHY_SMOKE_ASM = $(BUILD_DIR)/lua-typography-smoke.asm
LUA_TYPOGRAPHY_SMOKE_ROM = $(BUILD_DIR)/lua-typography-smoke.rom
LUA_TYPOGRAPHY_WASM_SRC = $(BUILD_DIR)/lua-typography-wasm.lua
LUA_TYPOGRAPHY_WASM_ASM = $(BUILD_DIR)/lua-typography-wasm.asm
LUA_TYPOGRAPHY_WASM_ROM = $(BUILD_DIR)/lua-typography-wasm.rom
LUA_TYPOGRAPHY_EXPECTED = $(TEST_DIR)/lua/typography.expected
LUA_RAYCASTER_SRC = examples/raycaster.lua
LUA_RAYCASTER_ASM = $(BUILD_DIR)/lua-raycaster.asm
LUA_RAYCASTER_ROM = $(BUILD_DIR)/lua-raycaster.rom
LUA_RAYCASTER_CLI_SRC = $(BUILD_DIR)/lua-raycaster-cli.lua
LUA_RAYCASTER_CLI_ASM = $(BUILD_DIR)/lua-raycaster-cli.asm
LUA_RAYCASTER_CLI_ROM = $(BUILD_DIR)/lua-raycaster-cli.rom
LUA_RAYCASTER_SMOKE_SRC = $(BUILD_DIR)/lua-raycaster-smoke.lua
LUA_RAYCASTER_SMOKE_ASM = $(BUILD_DIR)/lua-raycaster-smoke.asm
LUA_RAYCASTER_SMOKE_ROM = $(BUILD_DIR)/lua-raycaster-smoke.rom
LUA_RAYCASTER_WASM_SRC = $(BUILD_DIR)/lua-raycaster-wasm.lua
LUA_RAYCASTER_WASM_ASM = $(BUILD_DIR)/lua-raycaster-wasm.asm
LUA_RAYCASTER_WASM_ROM = $(BUILD_DIR)/lua-raycaster-wasm.rom
LUA_RAYCASTER_EXPECTED = $(TEST_DIR)/lua/raycaster.expected
CONFORMANCE_EXPECTED = $(TEST_DIR)/conformance.expected
DEBUGGER_SCRIPT = $(TEST_DIR)/debugger.in
DEBUGGER_EXPECTED = $(TEST_DIR)/debugger.expected
LUA_DEBUG_SCRIPT = $(TEST_DIR)/lua/control.dbg.in
LUA_DEBUG_EXPECTED = $(TEST_DIR)/lua/control.dbg.expected
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
