# Programming for Performance and Analysis

This project contains runnable C++ implementations for the performance-analysis topics in the resume bullets:

- bounded producer-consumer queue using `std::unique_lock` and `std::condition_variable`
- false-sharing benchmark for cache-contention diagnosis with `perf c2c` and optional PAPI detection
- two-level cache simulator with LRU, quad-age, and Belady replacement policies
- inclusive, exclusive, NINE, and hybrid cache-management protocols
- PC-based and address-based stride prefetchers

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Producer-Consumer Demo

```bash
./build/pc_demo --items 1000000 --producers 4 --consumers 4 --queue 2048
```

The queue implementation lives in `include/blocking_queue.hpp`. Producers block when the queue is full, consumers block when it is empty, and `close()` wakes all waiters once production is complete.

## Cache Simulator

The trace format is:

```text
R 0x1000 0x0000
W 0x3000 0x0040
```

Each row contains operation, program counter, and memory address. A sample trace is provided at `data/sample.trace`.

Example runs:

```bash
./build/cache_sim --trace data/sample.trace --policy lru --protocol inclusive --prefetch none
./build/cache_sim --trace data/sample.trace --policy quad-age --protocol nine --prefetch pc-stride
./build/cache_sim --trace data/sample.trace --policy belady --protocol hybrid --prefetch addr-stride
```

Useful experiment matrix:

```bash
for policy in lru quad-age belady; do
  for protocol in inclusive exclusive nine hybrid; do
    for prefetch in none pc-stride addr-stride; do
      ./build/cache_sim \
        --trace data/sample.trace \
        --policy "$policy" \
        --protocol "$protocol" \
        --prefetch "$prefetch"
    done
  done
done
```

The simulator reports L1 miss rate, L2 miss rate after L1 misses, memory reads, prefetches issued, useful prefetches, and inclusive invalidations.

## Validation

The project includes a blocking-queue unit test and a cache-simulator golden test:

```bash
clang++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Iinclude \
  tests/blocking_queue_test.cpp -o build/blocking_queue_test
./build/blocking_queue_test

./build/cache_sim --trace data/hand_trace.trace \
  --l1-size 128 --l1-assoc 2 --l2-size 256 --l2-assoc 2 \
  --policy lru --protocol inclusive --prefetch none
tests/cache_sim_golden.sh ./build/cache_sim
```

The hand-traced 20-instruction sequence validates that the inclusive LRU model reports 4 L1 hits, 16 L1 misses, 6 L2 hits, 10 memory reads, and 2 inclusion invalidations.

## Replacement Policies

`lru` evicts the least recently touched cache line.

`quad-age` keeps a 2-bit age for each line. Touched lines are promoted to age 3, and other lines decay toward 0. Victim selection prefers the lowest age and then the oldest timestamp.

`belady` uses the full trace to evict the line whose next access is farthest in the future. It is an offline upper bound, not a hardware-realistic policy.

## Cache Protocols

`inclusive` keeps L1 contents backed by L2 and invalidates L1 lines when their L2 copy is evicted.

`exclusive` keeps data in either L1 or L2. L2 hits are moved into L1, and L1 victims move down to L2.

`nine` means non-inclusive, non-exclusive. L1 and L2 are managed independently with no inclusion invalidations.

`hybrid` models a victim-cache style hierarchy: L1 fills do not always duplicate in L2, but L1 evictions are inserted into L2. Prefetched lines may also be installed in L2 to keep speculative data from disappearing too quickly.

## False-Sharing Benchmark

```bash
./build/false_sharing_bench --threads 8 --iterations 50000000
```

The benchmark compares compact adjacent counters against `alignas(64)` padded counters. On a local macOS arm64 run with 8 threads and 50M atomic increments per thread, padded counters were 11.9x faster at the median across five runs, with observed speedups from 9.8x to 13.3x. This is the concrete false-sharing signal: adjacent counters force cache-line ownership bouncing, while padded counters keep each thread on a separate line.

## Experiment Result

`data/interleaved_stride.trace` contains two independent stride streams interleaved by program counter. With no prefetching, L1 miss rate was 100% and memory reads were 24. The PC-stride prefetcher learned both streams independently, reducing L1 miss rate to 33.3% and memory reads to 8, with 16 useful prefetches. The address-stride prefetcher issued no prefetches on the same trace because the global address stream alternated between large positive and negative jumps.

```bash
./build/cache_sim --trace data/interleaved_stride.trace --policy lru --protocol inclusive --prefetch none
./build/cache_sim --trace data/interleaved_stride.trace --policy lru --protocol inclusive --prefetch pc-stride
./build/cache_sim --trace data/interleaved_stride.trace --policy lru --protocol inclusive --prefetch addr-stride
```

## Resume-Ready Bullets

- Diagnosed false sharing in an 8-thread C++ atomic-counter benchmark and improved throughput by 11.9x median using cache-line padding.
- Validated a two-level cache simulator against a hand-traced 20-instruction sequence covering L1 hits, L2 hits, memory reads, and inclusive invalidations.
- Compared PC-stride and address-stride prefetchers on interleaved streams; PC-stride reduced L1 miss rate from 100% to 33.3% while address-stride failed to learn the alternating global pattern.

See `docs/perf.md` for `perf c2c` and PAPI commands.
