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

TOOL_NAMES = vm asm dis rom
TOOLS      = $(foreach tool,$(TOOL_NAMES),$(BUILD_DIR)/0x$(tool))
COMMON_OBJ = $(BUILD_DIR)/tools_common.o

LIB_STATIC = $(BUILD_DIR)/lib0xvm.a
LIB_SHARED = $(BUILD_DIR)/lib0xvm.so
CONFORMANCE_ROM = $(BUILD_DIR)/conformance-v2.rom
CONFORMANCE_EXPECTED = $(TEST_DIR)/conformance.expected
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
all: libs $(TOOLS) $(TEST_BIN) $(CONFORMANCE_ROM) ## Build libraries, tools, tests, and conformance ROM

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

$(COMMON_OBJ): $(APPS_DIR)/common/tools.c $(APPS_DIR)/common/tools.h | $(BUILD_DIR)
	@echo "[INFO] Compiling tool helpers"
	$(CC) $(CFLAGS) -c -o $@ $<

$(LIB_STATIC): $(OBJ) | $(BUILD_DIR)
	@echo "[INFO] Building lib0xvm (static)"
	ar rcs $@ $(OBJ)

$(LIB_SHARED): $(OBJ) | $(BUILD_DIR)
	@echo "[INFO] Building lib0xvm (shared)"
	$(CC) $(SHARED_FLAG) -o $@ $(OBJ) $(LDFLAGS)

.PRECIOUS: $(OBJ) $(TEST_OBJ) $(COMMON_OBJ)

$(TOOLS): $(BUILD_DIR)/0x%: $(APPS_DIR)/%/main.c $(COMMON_OBJ) $(LIB_STATIC) | $(BUILD_DIR)
	@echo "[INFO] Building tool '0x$*'"
	$(CC) $(CFLAGS) -c $< -o $(BUILD_DIR)/app_0x$*.o
	$(CC) $(BUILD_DIR)/app_0x$*.o $(COMMON_OBJ) $(LIB_STATIC) -o $@ $(LDFLAGS)

$(TEST_MAIN): $(APPS_DIR)/test/main.c | $(BUILD_DIR)
	@echo "[INFO] Compiling test runner"
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_BIN): $(TEST_MAIN) $(TEST_OBJ) $(COMMON_OBJ) $(LIB_STATIC) | $(BUILD_DIR)
	@echo "[INFO] Building 0xtest"
	$(CC) $(TEST_MAIN) $(TEST_OBJ) $(COMMON_OBJ) $(LIB_STATIC) -o $@ $(LDFLAGS)

-include $(DEP) $(TEST_DEP) $(TEST_MAIN:.o=.d) $(COMMON_OBJ:.o=.d) \
	$(patsubst %,$(BUILD_DIR)/app_0x%.d,$(TOOL_NAMES))

$(CONFORMANCE_ROM): $(TEST_DIR)/conformance.asm $(STDLIB_SRC) $(BUILD_DIR)/0xasm | $(BUILD_DIR)
	@echo "[INFO] Building conformance ROM"
	@./$(BUILD_DIR)/0xasm -f $< -o $@

.PHONY: conformance
conformance: $(CONFORMANCE_ROM) ## Build the executable v2 conformance ROM

.PHONY: conformance-check
conformance-check: conformance $(BUILD_DIR)/0xvm ## Run the conformance ROM through the CLI host
	@echo "[RUN] CLI conformance check"
	@printf 'B-7\nhey\n' | ./$(BUILD_DIR)/0xvm -f $(CONFORMANCE_ROM) 2>/dev/null \
		| sed '/^\[INFO\] /d' > $(BUILD_DIR)/conformance-actual.txt
	@diff -u $(CONFORMANCE_EXPECTED) $(BUILD_DIR)/conformance-actual.txt

.PHONY: test
test: $(TEST_BIN) $(BUILD_DIR)/0xvm ## Build and run the test app and CLI conformance check
	@echo "[RUN] 0xtest"
	@$(TEST_BIN)
	@$(MAKE) --no-print-directory conformance-check

.PHONY: clean
clean: ## Remove all build files, libraries, and tools
	rm -rf $(BUILD_DIR)

.PHONY: help
help: ## Show this help message
	@echo "Available commands:"
	@grep -h -E '^[a-zA-Z0-9_.-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-25s\033[0m %s\n", $$1, $$2}'
