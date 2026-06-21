#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if __has_include(<papi.h>)
#define HAS_PAPI 1
#include <papi.h>
#else
#define HAS_PAPI 0
#endif

namespace {

struct CompactCounter {
    std::atomic<std::uint64_t> value{0};
};

struct alignas(64) PaddedCounter {
    std::atomic<std::uint64_t> value{0};
};

std::uint64_t parse_arg(char** argv, int argc, std::string_view name, std::uint64_t fallback) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (argv[i] == name) {
            return std::stoull(argv[i + 1]);
        }
    }
    return fallback;
}

template <typename Counter>
double run_case(std::uint64_t threads, std::uint64_t iterations) {
    std::vector<Counter> counters(threads);
    const auto started = std::chrono::steady_clock::now();

    std::vector<std::thread> workers;
    for (std::uint64_t t = 0; t < threads; ++t) {
        workers.emplace_back([&, t] {
            for (std::uint64_t i = 0; i < iterations; ++i) {
                counters[t].value.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    const auto finished = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(finished - started).count();
}

void print_papi_status() {
#if HAS_PAPI
    const int version = PAPI_library_init(PAPI_VER_CURRENT);
    if (version == PAPI_VER_CURRENT) {
        std::cout << "  papi: available\n";
    } else {
        std::cout << "  papi: detected but initialization failed\n";
    }
#else
    std::cout << "  papi: not available at compile time\n";
#endif
}

} // namespace

int main(int argc, char** argv) {
    const auto threads = parse_arg(argv, argc, "--threads", std::thread::hardware_concurrency());
    const auto iterations = parse_arg(argv, argc, "--iterations", 10'000'000);

    std::cout << "false_sharing_benchmark\n";
    std::cout << "  threads: " << threads << '\n';
    std::cout << "  iterations_per_thread: " << iterations << '\n';
    print_papi_status();

    const double compact_seconds = run_case<CompactCounter>(threads, iterations);
    const double padded_seconds = run_case<PaddedCounter>(threads, iterations);

    std::cout << "  compact_seconds: " << compact_seconds << '\n';
    std::cout << "  padded_seconds: " << padded_seconds << '\n';
    std::cout << "  speedup_from_padding: " << compact_seconds / padded_seconds << '\n';
}
