# SPDX-License-Identifier: MIT
# 0xluac golden programs, Lua capstones, and Lua-specific checks.
# Included by the top-level Makefile.

$(BUILD_DIR)/lua-%.asm: $(TEST_DIR)/lua/%.lua $(LUA_MODULE_SRCS) \
                        $(LUAC_MODULES) $(PYPROJECT) | $(BUILD_DIR)
	@echo "[INFO] Compiling Lua source $*"
	@python3 -m luac compile $< -o $@ -m $(BUILD_DIR)/lua-$*.lumap

$(BUILD_DIR)/lua-%.lumap: $(TEST_DIR)/lua/%.lua $(LUA_MODULE_SRCS) \
                          $(LUAC_MODULES) $(PYPROJECT) | $(BUILD_DIR)
	@echo "[INFO] Compiling Lua source map $*"
	@python3 -m luac compile $< -o $(BUILD_DIR)/lua-$*.asm -m $@

$(BUILD_DIR)/lua-%.rom: $(BUILD_DIR)/lua-%.asm $(BUILD_DIR)/0xasm
	@echo "[INFO] Assembling Lua output $*"
	@./$(BUILD_DIR)/0xasm -f $< -o $@

$(LUA_DEVICE_SMOKE_SRC): $(TEST_DIR)/lua/devices.lua | $(BUILD_DIR)
	@sed 's/^const ENABLE = 0$$/const ENABLE = 1/' $< > $@

$(BUILD_DIR)/lua-devices-smoke.asm: $(LUA_DEVICE_SMOKE_SRC) $(LUAC_MODULES) $(PYPROJECT) | $(BUILD_DIR)
	@echo "[INFO] Compiling Lua device smoke source"
	@python3 -m luac compile $< -o $@

$(LUA_TYPOGRAPHY_ASM): $(LUA_TYPOGRAPHY_SRC) $(LUAC_MODULES) $(PYPROJECT) | $(BUILD_DIR)
	@echo "[INFO] Compiling Lua typography source"
	@python3 -m luac compile $< -o $@ -m $(BUILD_DIR)/lua-typography.lumap

$(LUA_TYPOGRAPHY_CLI_SRC): $(LUA_TYPOGRAPHY_SRC) | $(BUILD_DIR)
	@sed 's/^const ENABLE = 1$$/const ENABLE = 0/' $< > $@

$(LUA_TYPOGRAPHY_CLI_ASM): $(LUA_TYPOGRAPHY_CLI_SRC) $(LUAC_MODULES) $(PYPROJECT) | $(BUILD_DIR)
	@echo "[INFO] Compiling Lua typography CLI source"
	@python3 -m luac compile $< -o $@

$(LUA_TYPOGRAPHY_SMOKE_SRC): $(LUA_TYPOGRAPHY_SRC) | $(BUILD_DIR)
	@sed -e 's/^const FRAME_MS = 66$$/const FRAME_MS = 1/' \
	     -e 's/^const MAX_FRAMES = 0$$/const MAX_FRAMES = 3/' $< > $@

$(LUA_TYPOGRAPHY_SMOKE_ASM): $(LUA_TYPOGRAPHY_SMOKE_SRC) $(LUAC_MODULES) $(PYPROJECT) | $(BUILD_DIR)
	@echo "[INFO] Compiling Lua typography smoke source"
	@python3 -m luac compile $< -o $@

$(LUA_TYPOGRAPHY_WASM_SRC): $(LUA_TYPOGRAPHY_SRC) | $(BUILD_DIR)
	@sed 's/^const REPORT = 0$$/const REPORT = 1/' $< > $@

$(LUA_TYPOGRAPHY_WASM_ASM): $(LUA_TYPOGRAPHY_WASM_SRC) $(LUAC_MODULES) $(PYPROJECT) | $(BUILD_DIR)
	@echo "[INFO] Compiling Lua typography browser source"
	@python3 -m luac compile $< -o $@

$(LUA_RAYCASTER_ASM): $(LUA_RAYCASTER_SRC) $(LUAC_MODULES) $(PYPROJECT) | $(BUILD_DIR)
	@echo "[INFO] Compiling Lua raycaster source"
	@python3 -m luac compile $< -o $@ -m $(BUILD_DIR)/lua-raycaster.lumap

$(LUA_RAYCASTER_CLI_SRC): $(LUA_RAYCASTER_SRC) | $(BUILD_DIR)
	@sed 's/^const ENABLE = 1$$/const ENABLE = 0/' $< > $@

$(LUA_RAYCASTER_CLI_ASM): $(LUA_RAYCASTER_CLI_SRC) $(LUAC_MODULES) $(PYPROJECT) | $(BUILD_DIR)
	@echo "[INFO] Compiling Lua raycaster CLI source"
	@python3 -m luac compile $< -o $@

$(LUA_RAYCASTER_SMOKE_SRC): $(LUA_RAYCASTER_SRC) | $(BUILD_DIR)
	@sed -e 's/^const FRAME_MS = 33$$/const FRAME_MS = 1/' \
	     -e 's/^const MAX_FRAMES = 0$$/const MAX_FRAMES = 3/' $< > $@

$(LUA_RAYCASTER_SMOKE_ASM): $(LUA_RAYCASTER_SMOKE_SRC) $(LUAC_MODULES) $(PYPROJECT) | $(BUILD_DIR)
	@echo "[INFO] Compiling Lua raycaster smoke source"
	@python3 -m luac compile $< -o $@

$(LUA_RAYCASTER_WASM_SRC): $(LUA_RAYCASTER_SRC) | $(BUILD_DIR)
	@sed -e 's/^const REPORT = 0$$/const REPORT = 1/' \
	     -e 's/^const FRAME_MS = 33$$/const FRAME_MS = 0/' $< > $@

$(LUA_RAYCASTER_WASM_ASM): $(LUA_RAYCASTER_WASM_SRC) $(LUAC_MODULES) $(PYPROJECT) | $(BUILD_DIR)
	@echo "[INFO] Compiling Lua raycaster browser source"
	@python3 -m luac compile $< -o $@

.PHONY: lua-device-check
ifeq ($(SDL_HOST),)
lua-device-check: $(LUA_DEVICE_SMOKE_ROM)
	@echo "[RUN] Lua device intrinsic check (SDL3 not found)"
else
lua-device-check: $(LUA_DEVICE_SMOKE_ROM) $(SDL_HOST)
	@echo "[RUN] Lua device intrinsic SDL smoke check (dummy video)"
	@out=$$(SDL_VIDEODRIVER=dummy ./$(SDL_HOST) -f $(LUA_DEVICE_SMOKE_ROM) -W 320 -H 240 2>&1); \
		printf '%s\n' "$$out" | grep -q 'VM_HALTED' || { printf '%s\n' "$$out"; exit 1; }; \
		printf '%s\n' "$$out" | grep -q 'DEVICE OK' || { printf '%s\n' "$$out"; exit 1; }; \
		printf '%s\n' "$$out" | grep -q 'ERRO' && { printf '%s\n' "$$out"; exit 1; }; \
		exit 0
endif

.PHONY: lua-typography-check
ifeq ($(SDL_HOST),)
lua-typography-check: $(LUA_TYPOGRAPHY_ROM) $(LUA_TYPOGRAPHY_CLI_ROM) \
                      $(LUA_TYPOGRAPHY_EXPECTED) $(BUILD_DIR)/0xvm
else
lua-typography-check: $(LUA_TYPOGRAPHY_ROM) $(LUA_TYPOGRAPHY_CLI_ROM) \
                      $(LUA_TYPOGRAPHY_SMOKE_ROM) $(LUA_TYPOGRAPHY_EXPECTED) \
                      $(BUILD_DIR)/0xvm $(SDL_HOST)
endif
	@echo "[RUN] Lua typography CLI check"
	@./$(BUILD_DIR)/0xvm -f $(LUA_TYPOGRAPHY_CLI_ROM) 2>/dev/null \
		| sed '/^\[INFO\] /d' > $(BUILD_DIR)/lua-typography-actual.txt
	@diff -u $(LUA_TYPOGRAPHY_EXPECTED) $(BUILD_DIR)/lua-typography-actual.txt
ifeq ($(SDL_HOST),)
	@echo "[RUN] Lua typography SDL check (SDL3 not found)"
else
	@echo "[RUN] Lua typography SDL smoke check (dummy video)"
	@out=$$(SDL_VIDEODRIVER=dummy ./$(SDL_HOST) -f $(LUA_TYPOGRAPHY_SMOKE_ROM) \
		-W 320 -H 240 2>&1); \
		printf '%s\n' "$$out" | grep -q 'VM_HALTED' || { printf '%s\n' "$$out"; exit 1; }; \
		printf '%s\n' "$$out" | grep -q 'TYPOGRAPHY OK' || { printf '%s\n' "$$out"; exit 1; }; \
		printf '%s\n' "$$out" | grep -q 'ERRO' && { printf '%s\n' "$$out"; exit 1; }; \
		exit 0
endif

.PHONY: lua-raycaster-check
ifeq ($(SDL_HOST),)
lua-raycaster-check: $(LUA_RAYCASTER_ROM) $(LUA_RAYCASTER_CLI_ROM) \
                     $(LUA_RAYCASTER_EXPECTED) $(BUILD_DIR)/0xvm
else
lua-raycaster-check: $(LUA_RAYCASTER_ROM) $(LUA_RAYCASTER_CLI_ROM) \
                     $(LUA_RAYCASTER_SMOKE_ROM) $(LUA_RAYCASTER_EXPECTED) \
                     $(BUILD_DIR)/0xvm $(SDL_HOST)
endif
	@echo "[RUN] Lua raycaster CLI check"
	@./$(BUILD_DIR)/0xvm -f $(LUA_RAYCASTER_CLI_ROM) 2>/dev/null \
		| sed '/^\[INFO\] /d' > $(BUILD_DIR)/lua-raycaster-actual.txt
	@diff -u $(LUA_RAYCASTER_EXPECTED) $(BUILD_DIR)/lua-raycaster-actual.txt
ifeq ($(SDL_HOST),)
	@echo "[RUN] Lua raycaster SDL check (SDL3 not found)"
else
	@echo "[RUN] Lua raycaster SDL smoke check (dummy video)"
	@out=$$(SDL_VIDEODRIVER=dummy ./$(SDL_HOST) -f $(LUA_RAYCASTER_SMOKE_ROM) \
		-W 320 -H 240 2>&1); \
		printf '%s\n' "$$out" | grep -q 'VM_HALTED' || { printf '%s\n' "$$out"; exit 1; }; \
		printf '%s\n' "$$out" | grep -q 'RAYCASTER OK' || { printf '%s\n' "$$out"; exit 1; }; \
		printf '%s\n' "$$out" | grep -q 'ERRO' && { printf '%s\n' "$$out"; exit 1; }; \
		exit 0
endif

.PHONY: lua-check
lua-check: $(LUA_ROMS) $(LUA_MAPS) $(BUILD_DIR)/0xvm ## Compile and run the Lua subset golden programs
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
