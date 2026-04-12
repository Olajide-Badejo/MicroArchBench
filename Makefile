# Makefile
# Convenience wrapper around CMake so common operations are one command.
# All real build logic lives in CMakeLists.txt -- this file just saves typing.
#
# Targets:
#   make               -- configure (if needed) + build
#   make configure     -- run cmake configure step only
#   make test          -- build then run ctest
#   make run-latency   -- run pointer_chase benchmark
#   make run-bandwidth -- run stream_bench benchmark
#   make run-flops     -- run peak_flops benchmark
#   make run-gemm      -- run gemm_tile_bench benchmark
#   make run-all       -- run full suite via scripts/run_suite.sh
#   make plots         -- generate roofline plots from last results run
#   make clean         -- remove build/ directory
#   make distclean     -- remove build/ and results/

BUILD_DIR   := build
BUILD_TYPE  := Release
GENERATOR   := Ninja
JOBS        := $(shell nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)

# CMake configuration flags -- override on the command line if needed:
#   make BENCH_AVX2=OFF
BENCH_AVX2    ?= ON
BENCH_FMA     ?= ON
BENCH_AVX512  ?= OFF
BENCH_PERF    ?= ON
BENCH_NUMA    ?= AUTO

CMAKE_FLAGS := \
    -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
    -DBENCH_ENABLE_AVX2=$(BENCH_AVX2) \
    -DBENCH_ENABLE_FMA=$(BENCH_FMA) \
    -DBENCH_ENABLE_AVX512=$(BENCH_AVX512) \
    -DBENCH_ENABLE_PERF=$(BENCH_PERF) \
    -DBENCH_WITH_NUMA=$(BENCH_NUMA) \
    -DBENCHMARK_DOWNLOAD_DEPENDENCIES=ON \
    -G $(GENERATOR)

# ---- Default target ----

.PHONY: all
all: $(BUILD_DIR)/CMakeCache.txt
	cmake --build $(BUILD_DIR) -j$(JOBS)

# ---- Configure ----

.PHONY: configure
configure: $(BUILD_DIR)/CMakeCache.txt

$(BUILD_DIR)/CMakeCache.txt:
	cmake -B $(BUILD_DIR) $(CMAKE_FLAGS)

# ---- Tests ----

.PHONY: test
test: all
	ctest --test-dir $(BUILD_DIR) -V --output-on-failure

# ---- Individual benchmarks ----

.PHONY: run-latency
run-latency: all
	$(BUILD_DIR)/pointer_chase \
	    --benchmark_format=console \
	    --benchmark_repetitions=3 \
	    --benchmark_min_warmup_time=0.5

.PHONY: run-bandwidth
run-bandwidth: all
	$(BUILD_DIR)/stream_bench \
	    --benchmark_format=console \
	    --benchmark_repetitions=3

.PHONY: run-flops
run-flops: all
	$(BUILD_DIR)/peak_flops \
	    --benchmark_format=console \
	    --benchmark_repetitions=5

.PHONY: run-gemm
run-gemm: all
	$(BUILD_DIR)/gemm_tile_bench \
	    --benchmark_format=console

# ---- Full suite ----

.PHONY: run-all
run-all: all
	bash scripts/run_suite.sh --no-freq-lock

# Variant with frequency locking (requires sudo)
.PHONY: run-all-locked
run-all-locked: all
	bash scripts/run_suite.sh

# ---- Plots only (re-process last results without re-running benchmarks) ----

.PHONY: plots
plots:
	@LATEST=$$(ls -dt results/raw/*/ 2>/dev/null | head -1); \
	if [ -z "$$LATEST" ]; then \
	    echo "No results found. Run 'make run-all' first."; exit 1; \
	fi; \
	echo "Post-processing: $$LATEST"; \
	python3 scripts/parse_perf.py "$$LATEST" > "$$LATEST/summary.json"; \
	python3 scripts/roofline.py "$$LATEST/summary.json" --output results/plots; \
	python3 scripts/plot_bandwidth_scaling.py "$$LATEST" --output results/plots; \
	python3 scripts/tile_predict.py "$$LATEST/summary.json"

# ---- Python deps ----

.PHONY: python-deps
python-deps:
	pip3 install -r requirements.txt

# ---- Clean ----

.PHONY: clean
clean:
	cmake --build $(BUILD_DIR) --target clean 2>/dev/null || true

.PHONY: distclean
distclean:
	rm -rf $(BUILD_DIR)
	rm -rf results/raw/*/
	rm -rf results/plots/*.pdf results/plots/*.png

# ---- Help ----

.PHONY: help
help:
	@echo ""
	@echo "CPU Microbenchmark Suite -- make targets"
	@echo ""
	@echo "  make                 Configure (if needed) and build everything"
	@echo "  make configure       Run cmake configure step only"
	@echo "  make test            Build and run correctness tests (ctest)"
	@echo "  make run-latency     Pointer-chase latency benchmark"
	@echo "  make run-bandwidth   STREAM bandwidth benchmark"
	@echo "  make run-flops       Peak FLOPS benchmark"
	@echo "  make run-gemm        GEMM tile validation benchmark"
	@echo "  make run-all         Full suite (no frequency locking)"
	@echo "  make run-all-locked  Full suite with frequency locking (sudo required)"
	@echo "  make plots           Re-generate plots from last run's JSON data"
	@echo "  make python-deps     Install Python dependencies (numpy, matplotlib, scipy)"
	@echo "  make clean           Remove compiled objects"
	@echo "  make distclean       Remove build/ and all result files"
	@echo ""
	@echo "Override CMake flags on the command line:"
	@echo "  make BENCH_AVX2=OFF run-bandwidth"
	@echo "  make BUILD_TYPE=Debug test"
	@echo ""
