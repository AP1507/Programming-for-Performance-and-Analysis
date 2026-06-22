#include "blocking_queue.hpp"

#include <cassert>
#include <cstdint>
#include <thread>
#include <vector>

int main() {
    BlockingQueue<int> queue(2);
    constexpr int items = 10'000;
    std::int64_t sum = 0;

    std::thread producer([&] {
        for (int i = 1; i <= items; ++i) {
            queue.push(i);
        }
        queue.close();
    });

    std::thread consumer([&] {
        while (auto value = queue.pop()) {
            sum += *value;
        }
    });

    producer.join();
    consumer.join();

    assert(sum == (static_cast<std::int64_t>(items) * (items + 1)) / 2);

    BlockingQueue<int> closed_queue(1);
    closed_queue.close();
    assert(!closed_queue.pop().has_value());
}
