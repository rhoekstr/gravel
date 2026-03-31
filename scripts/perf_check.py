#!/usr/bin/env python3
"""Compare Google Benchmark JSON output against a baseline.

Usage:
    python scripts/perf_check.py --baseline perf_baseline.json --current perf_current.json [--threshold 1.10]

Exits with code 1 if any benchmark exceeds the threshold (default 110%).
"""

import argparse
import json
import sys


def load_benchmarks(path: str) -> dict[str, float]:
    """Load benchmark name -> real_time mapping from Google Benchmark JSON."""
    with open(path) as f:
        data = json.load(f)
    result = {}
    for bench in data.get("benchmarks", []):
        if bench.get("run_type") == "iteration":
            result[bench["name"]] = bench["real_time"]
    return result


def main():
    parser = argparse.ArgumentParser(description="Performance regression check")
    parser.add_argument("--baseline", required=True, help="Baseline JSON file")
    parser.add_argument("--current", required=True, help="Current JSON file")
    parser.add_argument("--threshold", type=float, default=1.10,
                        help="Maximum allowed ratio current/baseline (default: 1.10)")
    args = parser.parse_args()

    baseline = load_benchmarks(args.baseline)
    current = load_benchmarks(args.current)

    if not baseline:
        print("ERROR: No benchmarks found in baseline file")
        sys.exit(1)

    regressions = []
    print(f"{'Benchmark':<50} {'Baseline':>12} {'Current':>12} {'Ratio':>8} {'Status':>8}")
    print("-" * 92)

    for name, base_time in sorted(baseline.items()):
        if name not in current:
            print(f"{name:<50} {'N/A':>12} {'MISSING':>12} {'---':>8} {'SKIP':>8}")
            continue

        cur_time = current[name]
        ratio = cur_time / base_time if base_time > 0 else 0.0
        status = "PASS" if ratio <= args.threshold else "FAIL"

        if status == "FAIL":
            regressions.append((name, ratio))

        print(f"{name:<50} {base_time:>12.2f} {cur_time:>12.2f} {ratio:>8.2f} {status:>8}")

    print()
    if regressions:
        print(f"FAILED: {len(regressions)} benchmark(s) exceeded {args.threshold:.0%} threshold:")
        for name, ratio in regressions:
            print(f"  - {name}: {ratio:.2f}x")
        sys.exit(1)
    else:
        print(f"PASSED: All benchmarks within {args.threshold:.0%} of baseline")
        sys.exit(0)


if __name__ == "__main__":
    main()
