# SPDX-License-Identifier: MIT
# Assembly-language example ROMs and their SDL smoke checks.
# Included by the top-level Makefile.

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
