# Profiling, benchmarks, and committed baseline comparisons.
# Included by the top-level Makefile.

BENCH_RUNS     ?= 20
THRESHOLD      ?= 0.25
BENCH_DIR      := bench
BENCH_SCRATCH  := $(BUILD_DIR)/bench
BENCH_RESULT   := $(BENCH_SCRATCH)/last.json
BENCH_BASELINE := $(BENCH_DIR)/baseline.json
PERF_BASELINE  := $(BENCH_DIR)/perf-profile.txt
BASELINE       ?= $(BENCH_BASELINE)
CURRENT        ?= $(BENCH_RESULT)

.PHONY: perf-check
perf-check: $(BUILD_DIR)/0xvm $(CONFORMANCE_ROM) ## Check CLI timing/profile output
	@echo "[RUN] CLI profile check"
	@printf 'B-7\nhey\n' | ./$(BUILD_DIR)/0xvm -f $(CONFORMANCE_ROM) -p 2> $(BUILD_DIR)/perf-check.err > /dev/null
	@grep -q '\[profile\] total ticks=' $(BUILD_DIR)/perf-check.err
	@grep -q '\[profile\] PUSH' $(BUILD_DIR)/perf-check.err

.PHONY: perf-update
perf-update: perf-check ## Save the latest profile as the committed baseline
	@mkdir -p $(BENCH_DIR)
	@cp $(BUILD_DIR)/perf-check.err $(PERF_BASELINE)
	@echo "[INFO] Saved profile baseline $(PERF_BASELINE)"

.PHONY: perf-compare
perf-compare: perf-check ## Diff the current CLI profile against the committed baseline
	@diff -u $(PERF_BASELINE) $(BUILD_DIR)/perf-check.err

.PHONY: bench
bench: $(BUILD_DIR)/0xvm $(CONFORMANCE_ROM) scripts/bench.py examples/statistics.asm ## Run repeatable benchmarks and save JSON to build/bench/
	@mkdir -p $(BENCH_SCRATCH)
	@python3 scripts/bench.py run --runs $(BENCH_RUNS) --output $(BENCH_RESULT)

.PHONY: bench-update
bench-update: bench ## Save the latest benchmark run as the committed baseline
	@mkdir -p $(BENCH_DIR)
	@cp $(BENCH_RESULT) $(BENCH_BASELINE)
	@echo "[INFO] Saved benchmark baseline $(BENCH_BASELINE)"

.PHONY: bench-compare
bench-compare: bench ## Compare current benchmarks with the committed baseline [BASELINE=... CURRENT=... THRESHOLD=...]
	@test -f "$(BASELINE)" || { echo "Missing baseline $(BASELINE); run make bench-update"; exit 1; }
	@python3 scripts/bench.py compare "$(BASELINE)" "$(CURRENT)" --threshold $(THRESHOLD)
