# SPDX-License-Identifier: MIT
# Native build rules: libraries, tools, test runner, and dependency files.
# Included by the top-level Makefile.

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
