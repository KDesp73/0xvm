# SPDX-License-Identifier: MIT
# Top-level Makefile for the stack VM.
# Configuration and rules live in focused files under make/.
.DEFAULT_GOAL := help

include make/config.mk
include make/core.mk
include make/examples.mk
include make/lua.mk
include make/tests.mk
include make/bench.mk
include make/hosts.mk

.PHONY: all
all: libs $(TOOLS) $(SDL_HOST) $(WASM_JS) $(WASM_APP) $(WASM_HTML) $(TEST_BIN) $(CONFORMANCE_ROM) $(SNAKE_ROM) $(TYPOGRAPHY_ROM) ## Build libraries, tools, hosts, tests, and ROMs

.PHONY: clean
clean: ## Remove all build files, libraries, and tools
	rm -rf $(BUILD_DIR)

.PHONY: help
help: ## Show this help message
	@echo "Available commands:"
	@grep -h -E '^[a-zA-Z0-9_.-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-25s\033[0m %s\n", $$1, $$2}'
