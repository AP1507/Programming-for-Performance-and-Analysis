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
./build/false_sharing_bench --threads 8 --iterations 10000000
```

The benchmark compares compact adjacent counters against `alignas(64)` padded counters. The speedup from padding gives a simple signal for cache-line contention.

See `docs/perf.md` for `perf c2c` and PAPI commands.
