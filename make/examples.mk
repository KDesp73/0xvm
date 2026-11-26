# SPDX-License-Identifier: MIT
# Assembly-language example ROMs and their SDL smoke checks.
# Included by the top-level Makefile.

$(BOOT_ROM): $(BOOT_SRC) $(TYPOGRAPHY_SRC) $(STDLIB_SRC) $(BUILD_DIR)/0xasm | $(BUILD_DIR)
	@echo "[INFO] Building boot ROM"
	@./$(BUILD_DIR)/0xasm -f $< -o $@

# Smoke builds cap the otherwise indefinite screen wait.
$(BOOT_SMOKE_SRC): $(BOOT_SRC) $(TYPOGRAPHY_SRC) | $(BUILD_DIR)
	@{ printf 'EQU BOOT_MAX_MS 500\n'; \
	   sed -e 's|INCLUDE "typography/|INCLUDE "../examples/typography/|' $<; \
	 } > $@

$(BOOT_SMOKE_ROM): $(BOOT_SMOKE_SRC) $(TYPOGRAPHY_SRC) $(STDLIB_SRC) $(BUILD_DIR)/0xasm
	@./$(BUILD_DIR)/0xasm -f $< -o $@

.PHONY: boot-check
ifeq ($(SDL_HOST),)
boot-check: $(BOOT_ROM) $(BUILD_DIR)/0xvm
else
boot-check: $(BOOT_ROM) $(BOOT_SMOKE_ROM) $(BUILD_DIR)/0xvm $(SDL_HOST)
endif
	@echo "[RUN] Boot CLI terminal check"
	@out=$$(./$(BUILD_DIR)/0xvm -f $(BOOT_ROM) 2>&1); \
		printf '%s\n' "$$out" | grep -q 'VM_HALTED' || { printf '%s\n' "$$out"; exit 1; }; \
		printf '%s\n' "$$out" | grep -q '▄▄▄' || { printf '%s\n' "$$out"; exit 1; }; \
		printf '%s\n' "$$out" | grep -q 'VMIM VERSION' || { printf '%s\n' "$$out"; exit 1; }; \
		printf '%s\n' "$$out" | grep -q 'HOUSE PALETTE' || { printf '%s\n' "$$out"; exit 1; }; \
		printf '%s\n' "$$out" | grep -q 'Breach' && { printf '%s\n' "$$out"; exit 1; }; \
		printf '%s\n' "$$out" | grep -q 'ERRO' && { printf '%s\n' "$$out"; exit 1; }; \
		exit 0
ifeq ($(SDL_HOST),)
	@echo "[RUN] Boot screen check skipped (SDL3 not found)"
else
	@echo "[RUN] Boot SDL screen check (dummy video)"
	@out=$$(SDL_VIDEODRIVER=dummy ./$(SDL_HOST) -f $(BOOT_SMOKE_ROM) -W 320 -H 240 2>&1); \
		printf '%s\n' "$$out" | grep -q 'VM_HALTED' || { printf '%s\n' "$$out"; exit 1; }; \
		printf '%s\n' "$$out" | grep -Eq 'pixels=[1-9]' || { printf '%s\n' "$$out"; exit 1; }; \
		printf '%s\n' "$$out" | grep -q 'VMIM VERSION' && { printf '%s\n' "$$out"; exit 1; }; \
		printf '%s\n' "$$out" | grep -q '▄▄▄' && { printf '%s\n' "$$out"; exit 1; }; \
		printf '%s\n' "$$out" | grep -q 'Breach' && { printf '%s\n' "$$out"; exit 1; }; \
		printf '%s\n' "$$out" | grep -q 'ERRO' && { printf '%s\n' "$$out"; exit 1; }; \
		exit 0
endif

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
