#include "blocking_queue.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct WorkItem {
    std::uint64_t id;
    std::uint64_t payload;
};

std::uint64_t mix(std::uint64_t x) {
    x ^= x >> 33U;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33U;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33U;
    return x;
}

std::uint64_t parse_arg(char** argv, int argc, std::string_view name, std::uint64_t fallback) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (argv[i] == name) {
            return std::stoull(argv[i + 1]);
        }
    }
    return fallback;
}

} // namespace

int main(int argc, char** argv) {
    const std::uint64_t items = parse_arg(argv, argc, "--items", 1'000'000);
    const std::uint64_t producers = parse_arg(argv, argc, "--producers", 2);
    const std::uint64_t consumers = parse_arg(argv, argc, "--consumers", 2);
    const std::uint64_t queue_capacity = parse_arg(argv, argc, "--queue", 1024);

    BlockingQueue<WorkItem> queue(queue_capacity);
    std::atomic<std::uint64_t> produced{0};
    std::atomic<std::uint64_t> consumed{0};
    std::atomic<std::uint64_t> checksum{0};

    const auto started = std::chrono::steady_clock::now();

    std::vector<std::thread> producer_threads;
    for (std::uint64_t p = 0; p < producers; ++p) {
        producer_threads.emplace_back([&, p] {
            while (true) {
                const auto id = produced.fetch_add(1, std::memory_order_relaxed);
                if (id >= items) {
                    break;
                }
                queue.push({id, mix(id + p * 1315423911ULL)});
            }
        });
    }

    std::vector<std::thread> consumer_threads;
    for (std::uint64_t c = 0; c < consumers; ++c) {
        consumer_threads.emplace_back([&, c] {
            while (auto item = queue.pop()) {
                const auto result = mix(item->payload ^ c);
                checksum.fetch_xor(result, std::memory_order_relaxed);
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& thread : producer_threads) {
        thread.join();
    }
    queue.close();
    for (auto& thread : consumer_threads) {
        thread.join();
    }

    const auto finished = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(finished - started).count();
    const auto throughput = static_cast<double>(consumed.load()) / elapsed;

    std::cout << "producer_consumer_summary\n";
    std::cout << "  producers: " << producers << '\n';
    std::cout << "  consumers: " << consumers << '\n';
    std::cout << "  queue_capacity: " << queue_capacity << '\n';
    std::cout << "  items: " << consumed.load() << '\n';
    std::cout << "  seconds: " << elapsed << '\n';
    std::cout << "  throughput_items_per_second: " << throughput << '\n';
    std::cout << "  checksum: " << checksum.load() << '\n';
}
