#!/usr/bin/env bash
set -euo pipefail

binary="${1:-./build/cache_sim}"

output="$("$binary" \
  --trace data/hand_trace.trace \
  --l1-size 128 \
  --l1-assoc 2 \
  --l2-size 256 \
  --l2-assoc 2 \
  --policy lru \
  --protocol inclusive \
  --prefetch none)"

grep -q "demand_accesses: 20" <<< "$output"
grep -q "l1_hits: 4" <<< "$output"
grep -q "l1_misses: 16" <<< "$output"
grep -q "l1_miss_rate: 0.8000" <<< "$output"
grep -q "l2_hits: 6" <<< "$output"
grep -q "l2_misses: 10" <<< "$output"
grep -q "memory_reads: 10" <<< "$output"
grep -q "inclusive_invalidations: 2" <<< "$output"
