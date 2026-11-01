#!/usr/bin/env bash
# Reproduce every benchmark table in README.md.
#
#   bash bench/reproduce.sh [out_dir=bench-results] [cpu=2]
#
# Builds three engine versions with identical flags and the same benchmark harness:
#   baseline : engine sources from commit 45fed4c (before any optimization)
#   step1    : baseline + event-log strings only built when logging is enabled
#   step2    : current working tree (step1 + pooled node allocator)
# then runs:
#   A) optimization table   - 7 interleaved rounds, 1M ops, empty book
#   B) depth sweep          - 5 interleaved rounds, 1M ops, 0 / 10k / 100k / 1M resting orders
#   C) ingest throughput    - 1M synthetic OrderFilled events, decoded + raw-log CSV, 3 runs each
# Needs: git, g++ (C++17), python3, taskset (util-linux), /usr/bin/time.
set -euo pipefail

BASELINE_REF=45fed4c
OUT=${1:-bench-results}
CPU=${2:-2}
ROOT=$(git rev-parse --show-toplevel)
cd "$ROOT"
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
FLAGS="-std=c++17 -O3 -DNDEBUG"

echo "== machine"
{ lscpu | grep -E "Model name" ; g++ --version | head -1; uname -r; } | tee "$OUT/machine.txt"

echo "== building baseline ($BASELINE_REF), step1, step2"
for v in baseline step1; do
    mkdir -p "$OUT/$v/include" "$OUT/$v/src"
    # every engine header + source from the baseline commit, so nothing mixes with the working tree
    for f in include/pch.h include/orders.h include/orderbook.h src/orderbook.cpp src/Order.cpp; do
        git show "$BASELINE_REF:$f" > "$OUT/$v/$f"
    done
done
# step1 = guard every event-log call so no strings are formatted when logging is off
sed -i -E 's/^(\s*)logEvent\(/\1if (eventLog) logEvent(/' "$OUT/step1/src/orderbook.cpp"

for v in baseline step1; do
    # only the benchmark harness comes from the working tree
    g++ $FLAGS -I"$OUT/$v/include" bench/benchmark.cpp "$OUT/$v/src/orderbook.cpp" "$OUT/$v/src/Order.cpp" \
        -o "$OUT/bench_$v"
done
g++ $FLAGS -Iinclude bench/benchmark.cpp src/orderbook.cpp src/Order.cpp -o "$OUT/bench_step2"
g++ $FLAGS -Iinclude src/*.cpp -o "$OUT/orderbook"

echo "== A) optimization table (7 rounds, empty book)"
python3 bench/compare.py "$OUT" 0 7 bench_baseline,bench_step1,bench_step2 "$CPU" | tee "$OUT/A_optimization.txt"

echo "== B) depth sweep (5 rounds)"
python3 bench/compare.py "$OUT" 0,10000,100000,1000000 5 bench_baseline,bench_step2 "$CPU" | tee "$OUT/B_depth.txt"

echo "== C) ingest throughput (1M synthetic events)"
python3 bench/gen_synthetic_fills.py 1000000 "$OUT/synthetic"
: > "$OUT/C_ingest.txt"
for f in decoded raw; do
    ls -l "$OUT/synthetic_$f.csv" | awk -v f=$f '{printf "%s file_bytes=%s\n", f, $5}' | tee -a "$OUT/C_ingest.txt"
    for i in 1 2 3; do
        /usr/bin/time -f "$f run$i wall_s=%e maxrss_kb=%M" taskset -c "$CPU" "$OUT/orderbook" ingest \
            "$OUT/synthetic_$f.csv" > "$OUT/ingest_$f.log" 2>> "$OUT/C_ingest.txt"
    done
    grep -E "events parsed|skipped|replayed|mismatches" "$OUT/ingest_$f.log" | sed "s/^/$f /" >> "$OUT/C_ingest.txt"
done
cat "$OUT/C_ingest.txt"
rm -f "$OUT"/synthetic_*.csv
echo "== done: results in $OUT"
