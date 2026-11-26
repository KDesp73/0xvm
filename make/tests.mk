# SPDX-License-Identifier: MIT
# Test fixtures, scripted debugger checks, and the aggregate test target.
# Included by the top-level Makefile.

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

.PHONY: debugger-check
debugger-check: $(BUILD_DIR)/0xdb examples/gcd.asm $(DEBUGGER_SCRIPT) $(DEBUGGER_EXPECTED) ## Run a scripted 0xdb session
	@echo "[RUN] 0xdb command check"
	@./$(BUILD_DIR)/0xdb -f examples/gcd.asm -x $(DEBUGGER_SCRIPT) \
		> $(BUILD_DIR)/debugger-actual.txt
	@diff -u $(DEBUGGER_EXPECTED) $(BUILD_DIR)/debugger-actual.txt

.PHONY: lua-debug-check
lua-debug-check: $(BUILD_DIR)/0xdb $(BUILD_DIR)/lua-control.rom \
                 $(BUILD_DIR)/lua-control.asm $(BUILD_DIR)/lua-control.lumap \
                 $(LUA_DEBUG_SCRIPT) $(LUA_DEBUG_EXPECTED) ## Step Lua source via 0xdb
	@echo "[RUN] 0xdb Lua source-map check"
	@./$(BUILD_DIR)/0xdb -f $(BUILD_DIR)/lua-control.rom \
		-s $(BUILD_DIR)/lua-control.asm -m $(BUILD_DIR)/lua-control.lumap \
		-x $(LUA_DEBUG_SCRIPT) > $(BUILD_DIR)/lua-debug-actual.txt
	@diff -u $(LUA_DEBUG_EXPECTED) $(BUILD_DIR)/lua-debug-actual.txt

.PHONY: test
test: $(TEST_BIN) $(BUILD_DIR)/0xvm ## Build and run the test app and conformance checks
	@echo "[RUN] 0xtest"
	@$(TEST_BIN)
	@$(MAKE) --no-print-directory conformance-check
	@$(MAKE) --no-print-directory lua-check
	@$(MAKE) --no-print-directory lua-device-check
	@$(MAKE) --no-print-directory lua-typography-check
	@$(MAKE) --no-print-directory lua-raycaster-check
	@$(MAKE) --no-print-directory debugger-check
	@$(MAKE) --no-print-directory lua-debug-check
	@$(MAKE) --no-print-directory snake-check
	@$(MAKE) --no-print-directory typography-check
	@$(MAKE) --no-print-directory sdl-check
ifeq ($(EMCC_FOUND),yes)
	@$(MAKE) --no-print-directory wasm-check
endif
