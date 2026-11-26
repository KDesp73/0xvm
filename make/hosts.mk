# SDL3 and browser/WASM host build and smoke-check targets.
# Included by the top-level Makefile.

.PHONY: wasm
ifeq ($(EMCC_FOUND),yes)
wasm: $(WASM_JS) $(WASM_APP) $(WASM_HTML) $(SNAKE_ROM) $(TYPOGRAPHY_ROM) \
      $(LUA_TYPOGRAPHY_ROM) $(LUA_RAYCASTER_ROM) ## Build the browser/WASM host and example ROMs
else
wasm:
	@echo "[WARN] emcc not found; skipping browser/WASM host"
endif

.PHONY: wasm-check
ifeq ($(EMCC_FOUND),yes)
ifeq ($(NODE_FOUND),yes)
wasm-check: $(WASM_JS) $(SNAKE_SMOKE_ROM) $(CONFORMANCE_ROM) $(TYPOGRAPHY_ROM) \
            $(LUA_DEVICE_SMOKE_ROM) $(LUA_TYPOGRAPHY_WASM_ROM) \
            $(LUA_RAYCASTER_WASM_ROM) ## Smoke-run Snake, conformance, typography, and Lua examples in the browser host under Node
	@echo "[RUN] WASM Snake smoke check"
	@$(NODE) tests/wasm-smoke.mjs $(WASM_JS) $(SNAKE_SMOKE_ROM) \
		$(CONFORMANCE_ROM) $(CONFORMANCE_EXPECTED) $(TYPOGRAPHY_ROM) \
		$(LUA_DEVICE_SMOKE_ROM) $(LUA_TYPOGRAPHY_WASM_ROM) \
		$(LUA_RAYCASTER_WASM_ROM)
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
