# Performance Analysis Notes

## Cache Contention with `perf c2c`

Build with optimizations:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Record cache-to-cache traffic:

```bash
perf c2c record -- ./build/false_sharing_bench --threads 8 --iterations 50000000
perf c2c report
```

In the compact-counter case, expect high HITM traffic because multiple threads update atomics sharing the same cache line. In the padded-counter case, each thread updates a separate line, so HITM traffic and elapsed time should drop.

If `perf c2c` is unavailable, these alternatives are still useful:

```bash
perf stat -e cache-references,cache-misses,L1-dcache-loads,L1-dcache-load-misses \
  ./build/false_sharing_bench --threads 8 --iterations 50000000
```

## Optional PAPI Workflow

`false_sharing_bench` checks for `papi.h` at compile time. If PAPI is installed and CMake can see the header, the binary reports that PAPI is available.

Typical system counters:

```bash
papi_avail
papi_native_avail
```

Example events to compare between compact and padded versions:

```text
PAPI_L1_DCM
PAPI_L2_DCM
PAPI_L3_TCM
PAPI_TOT_CYC
```

The current benchmark is intentionally small and portable. On a Linux machine with PAPI development headers installed, the next step would be to link against PAPI and wrap the compact and padded regions with `PAPI_start_counters` / `PAPI_stop_counters`.

## Cache Simulator Analysis

Use a larger trace to make protocol differences meaningful. The sample trace is tiny and exists for smoke testing.

Suggested comparisons:

```bash
./build/cache_sim --trace your.trace --policy lru --protocol inclusive --prefetch none
./build/cache_sim --trace your.trace --policy lru --protocol exclusive --prefetch none
./build/cache_sim --trace your.trace --policy lru --protocol nine --prefetch none
./build/cache_sim --trace your.trace --policy lru --protocol hybrid --prefetch none
```

Then vary replacement policy:

```bash
./build/cache_sim --trace your.trace --policy quad-age --protocol inclusive --prefetch none
./build/cache_sim --trace your.trace --policy belady --protocol inclusive --prefetch none
```

Then vary prefetcher:

```bash
./build/cache_sim --trace your.trace --policy lru --protocol inclusive --prefetch pc-stride
./build/cache_sim --trace your.trace --policy lru --protocol inclusive --prefetch addr-stride
```

Report the key results as L1 miss rate, L2 miss rate on L1 misses, total memory reads, and useful-prefetch rate.
