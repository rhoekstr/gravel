#!/usr/bin/env bash
# PGO (Profile-Guided Optimization) build workflow.
#
# Usage:
#   ./scripts/pgo_build.sh [--graph path/to/graph.gravel.meta] [--ch path/to/graph.gravel.ch]
#
# Steps:
#   1. Build with -fprofile-generate
#   2. Run benchmark suite to generate profiles
#   3. Rebuild with -fprofile-use
#   4. Run benchmarks again and compare

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
PGO_DIR="${PROJECT_DIR}/build_pgo/profiles"

echo "=== PGO Build Workflow ==="

# Step 1: Instrumented build
echo "[1/4] Building with -fprofile-generate..."
cmake -B "${PROJECT_DIR}/build_pgo" -S "${PROJECT_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DGRAVEL_BUILD_BENCH=ON \
    -DGRAVEL_ENABLE_PGO=ON \
    -DGRAVEL_PGO_GENERATE=ON \
    -DGRAVEL_PGO_DIR="${PGO_DIR}"
cmake --build "${PROJECT_DIR}/build_pgo" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

# Step 2: Generate profiles
echo "[2/4] Running benchmarks to generate profiles..."
mkdir -p "${PGO_DIR}"
"${PROJECT_DIR}/build_pgo/bench/gravel_perf" \
    --benchmark_format=json \
    --benchmark_out="${PROJECT_DIR}/build_pgo/pgo_profile_run.json" \
    --benchmark_min_time=0.5s || true

# Step 3: Optimized build
echo "[3/4] Rebuilding with -fprofile-use..."
cmake -B "${PROJECT_DIR}/build_pgo_opt" -S "${PROJECT_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DGRAVEL_BUILD_BENCH=ON \
    -DGRAVEL_ENABLE_PGO=ON \
    -DGRAVEL_PGO_DIR="${PGO_DIR}"
cmake --build "${PROJECT_DIR}/build_pgo_opt" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

# Step 4: Run optimized benchmarks
echo "[4/4] Running optimized benchmarks..."
"${PROJECT_DIR}/build_pgo_opt/bench/gravel_perf" \
    --benchmark_format=json \
    --benchmark_out="${PROJECT_DIR}/build_pgo_opt/pgo_optimized.json" \
    --benchmark_min_time=1s || true

echo ""
echo "=== PGO Build Complete ==="
echo "Profile run:   build_pgo/pgo_profile_run.json"
echo "Optimized run: build_pgo_opt/pgo_optimized.json"
echo ""
echo "Compare with:"
echo "  python scripts/perf_check.py --baseline build_pgo/pgo_profile_run.json --current build_pgo_opt/pgo_optimized.json"
