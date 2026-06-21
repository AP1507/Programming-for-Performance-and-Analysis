#include <algorithm>
#include <cctype>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kNever = std::numeric_limits<std::size_t>::max();

enum class ReplacementPolicy {
    Lru,
    QuadAge,
    Belady,
};

enum class Protocol {
    Inclusive,
    Exclusive,
    Nine,
    Hybrid,
};

enum class PrefetcherKind {
    None,
    PcStride,
    AddressStride,
};

struct Access {
    char op{'R'};
    std::uint64_t pc{0};
    std::uint64_t address{0};
};

struct CacheConfig {
    std::size_t size_bytes{32 * 1024};
    std::size_t line_size{64};
    std::size_t associativity{8};
    ReplacementPolicy policy{ReplacementPolicy::Lru};
};

struct CacheLine {
    std::uint64_t tag{0};
    std::uint64_t line_address{0};
    std::size_t last_used{0};
    std::uint8_t age{3};
    bool valid{false};
    bool prefetched{false};
};

struct CacheResult {
    bool hit{false};
    bool useful_prefetch{false};
};

struct InsertResult {
    std::optional<CacheLine> evicted;
};

struct Stats {
    std::uint64_t demand_accesses{0};
    std::uint64_t l1_hits{0};
    std::uint64_t l1_misses{0};
    std::uint64_t l2_hits{0};
    std::uint64_t l2_misses{0};
    std::uint64_t memory_reads{0};
    std::uint64_t prefetches_issued{0};
    std::uint64_t prefetch_hits{0};
    std::uint64_t prefetch_memory_reads{0};
    std::uint64_t useful_prefetches{0};
    std::uint64_t invalidations{0};
    std::uint64_t write_accesses{0};
};

std::string lower(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

std::uint64_t parse_u64(const std::string& value) {
    std::size_t parsed = 0;
    const auto result = std::stoull(value, &parsed, 0);
    if (parsed != value.size()) {
        throw std::invalid_argument("bad integer: " + value);
    }
    return result;
}

std::string arg_value(int argc, char** argv, std::string_view name, std::string fallback) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (argv[i] == name) {
            return argv[i + 1];
        }
    }
    return fallback;
}

bool has_flag(int argc, char** argv, std::string_view name) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == name) {
            return true;
        }
    }
    return false;
}

ReplacementPolicy parse_policy(std::string value) {
    value = lower(std::move(value));
    if (value == "lru") {
        return ReplacementPolicy::Lru;
    }
    if (value == "quad-age" || value == "quad_age" || value == "quad") {
        return ReplacementPolicy::QuadAge;
    }
    if (value == "belady" || value == "optimal" || value == "opt") {
        return ReplacementPolicy::Belady;
    }
    throw std::invalid_argument("unknown replacement policy: " + value);
}

Protocol parse_protocol(std::string value) {
    value = lower(std::move(value));
    if (value == "inclusive") {
        return Protocol::Inclusive;
    }
    if (value == "exclusive") {
        return Protocol::Exclusive;
    }
    if (value == "nine" || value == "non-inclusive" || value == "non-inclusive-non-exclusive") {
        return Protocol::Nine;
    }
    if (value == "hybrid") {
        return Protocol::Hybrid;
    }
    throw std::invalid_argument("unknown cache protocol: " + value);
}

PrefetcherKind parse_prefetcher(std::string value) {
    value = lower(std::move(value));
    if (value == "none" || value == "off") {
        return PrefetcherKind::None;
    }
    if (value == "pc-stride" || value == "pc") {
        return PrefetcherKind::PcStride;
    }
    if (value == "addr-stride" || value == "address-stride" || value == "address") {
        return PrefetcherKind::AddressStride;
    }
    throw std::invalid_argument("unknown prefetcher: " + value);
}

std::string policy_name(ReplacementPolicy policy) {
    switch (policy) {
    case ReplacementPolicy::Lru:
        return "lru";
    case ReplacementPolicy::QuadAge:
        return "quad-age";
    case ReplacementPolicy::Belady:
        return "belady";
    }
    return "unknown";
}

std::string protocol_name(Protocol protocol) {
    switch (protocol) {
    case Protocol::Inclusive:
        return "inclusive";
    case Protocol::Exclusive:
        return "exclusive";
    case Protocol::Nine:
        return "nine";
    case Protocol::Hybrid:
        return "hybrid";
    }
    return "unknown";
}

std::string prefetcher_name(PrefetcherKind prefetcher) {
    switch (prefetcher) {
    case PrefetcherKind::None:
        return "none";
    case PrefetcherKind::PcStride:
        return "pc-stride";
    case PrefetcherKind::AddressStride:
        return "addr-stride";
    }
    return "unknown";
}

std::vector<Access> load_trace(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("could not open trace: " + path);
    }

    std::vector<Access> trace;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream input(line);
        std::string op;
        std::string pc;
        std::string address;
        if (!(input >> op >> pc >> address)) {
            throw std::runtime_error("bad trace line: " + line);
        }
        trace.push_back({static_cast<char>(std::toupper(op[0])), parse_u64(pc), parse_u64(address)});
    }
    return trace;
}

class FutureUseTable {
public:
    FutureUseTable(const std::vector<Access>& trace, std::size_t line_size) {
        for (std::size_t index = 0; index < trace.size(); ++index) {
            future_[trace[index].address / line_size].push_back(index);
        }
    }

    void consume(std::uint64_t line_address, std::size_t index) {
        auto it = future_.find(line_address);
        if (it != future_.end() && !it->second.empty() && it->second.front() == index) {
            it->second.pop_front();
        }
    }

    std::size_t next_use(std::uint64_t line_address) const {
        auto it = future_.find(line_address);
        if (it == future_.end() || it->second.empty()) {
            return kNever;
        }
        return it->second.front();
    }

private:
    std::unordered_map<std::uint64_t, std::deque<std::size_t>> future_;
};

class Cache {
public:
    Cache(std::string name, CacheConfig config, FutureUseTable* future)
        : name_(std::move(name)), config_(config), future_(future) {
        if (config_.line_size == 0 || config_.associativity == 0 ||
            config_.size_bytes < config_.line_size * config_.associativity) {
            throw std::invalid_argument(name_ + " has an invalid cache configuration");
        }
        sets_.resize(config_.size_bytes / (config_.line_size * config_.associativity));
    }

    CacheResult access(std::uint64_t address, std::size_t cycle, bool is_prefetch = false) {
        const auto line_address = address / config_.line_size;
        auto& set = sets_[set_index(line_address)];
        for (auto& line : set) {
            if (line.valid && line.line_address == line_address) {
                const bool useful = !is_prefetch && line.prefetched;
                line.last_used = cycle;
                line.age = 3;
                line.prefetched = false;
                decay_others(set, line.line_address);
                return {true, useful};
            }
        }
        return {false, false};
    }

    bool contains(std::uint64_t address) const {
        const auto line_address = address / config_.line_size;
        const auto& set = sets_[set_index(line_address)];
        return std::any_of(set.begin(), set.end(), [&](const CacheLine& line) {
            return line.valid && line.line_address == line_address;
        });
    }

    InsertResult insert(std::uint64_t address, std::size_t cycle, bool prefetched = false) {
        const auto line_address = address / config_.line_size;
        auto& set = sets_[set_index(line_address)];
        for (auto& line : set) {
            if (line.valid && line.line_address == line_address) {
                line.last_used = cycle;
                line.age = 3;
                line.prefetched = line.prefetched || prefetched;
                decay_others(set, line.line_address);
                return {};
            }
        }

        CacheLine incoming;
        incoming.valid = true;
        incoming.tag = tag(line_address);
        incoming.line_address = line_address;
        incoming.last_used = cycle;
        incoming.age = 3;
        incoming.prefetched = prefetched;

        if (set.size() < config_.associativity) {
            decay_all(set);
            set.push_back(incoming);
            return {};
        }

        const auto victim = victim_index(set);
        CacheLine evicted = set[victim];
        decay_all(set);
        set[victim] = incoming;
        return {evicted};
    }

    bool invalidate_line_address(std::uint64_t line_address) {
        auto& set = sets_[set_index(line_address)];
        const auto before = set.size();
        set.erase(std::remove_if(set.begin(), set.end(), [&](const CacheLine& line) {
                      return line.valid && line.line_address == line_address;
                  }),
                  set.end());
        return set.size() != before;
    }

    bool remove(std::uint64_t address) {
        return invalidate_line_address(address / config_.line_size);
    }

private:
    std::size_t set_index(std::uint64_t line_address) const {
        return line_address % sets_.size();
    }

    std::uint64_t tag(std::uint64_t line_address) const {
        return line_address / sets_.size();
    }

    void decay_all(std::vector<CacheLine>& set) const {
        if (config_.policy != ReplacementPolicy::QuadAge) {
            return;
        }
        for (auto& line : set) {
            if (line.age > 0) {
                --line.age;
            }
        }
    }

    void decay_others(std::vector<CacheLine>& set, std::uint64_t touched_line_address) const {
        if (config_.policy != ReplacementPolicy::QuadAge) {
            return;
        }
        for (auto& line : set) {
            if (line.line_address != touched_line_address && line.age > 0) {
                --line.age;
            }
        }
    }

    std::size_t victim_index(const std::vector<CacheLine>& set) const {
        if (config_.policy == ReplacementPolicy::Belady) {
            std::size_t victim = 0;
            std::size_t farthest = 0;
            for (std::size_t i = 0; i < set.size(); ++i) {
                const auto next = future_ == nullptr ? kNever : future_->next_use(set[i].line_address);
                if (next == kNever) {
                    return i;
                }
                if (next > farthest) {
                    farthest = next;
                    victim = i;
                }
            }
            return victim;
        }

        if (config_.policy == ReplacementPolicy::QuadAge) {
            std::size_t victim = 0;
            for (std::size_t i = 1; i < set.size(); ++i) {
                if (std::pair{set[i].age, set[i].last_used} < std::pair{set[victim].age, set[victim].last_used}) {
                    victim = i;
                }
            }
            return victim;
        }

        std::size_t victim = 0;
        for (std::size_t i = 1; i < set.size(); ++i) {
            if (set[i].last_used < set[victim].last_used) {
                victim = i;
            }
        }
        return victim;
    }

    std::string name_;
    CacheConfig config_;
    FutureUseTable* future_;
    std::vector<std::vector<CacheLine>> sets_;
};

class Prefetcher {
public:
    explicit Prefetcher(PrefetcherKind kind, std::size_t line_size) : kind_(kind), line_size_(line_size) {}

    std::optional<std::uint64_t> observe(const Access& access) {
        if (kind_ == PrefetcherKind::None) {
            return std::nullopt;
        }

        const std::int64_t line = static_cast<std::int64_t>(access.address / line_size_);
        if (kind_ == PrefetcherKind::AddressStride) {
            return update_state(global_, line);
        }

        return update_state(by_pc_[access.pc], line);
    }

private:
    struct State {
        bool initialized{false};
        std::int64_t last_line{0};
        std::int64_t stride{0};
        std::uint8_t confidence{0};
    };

    std::optional<std::uint64_t> update_state(State& state, std::int64_t line) const {
        if (!state.initialized) {
            state.initialized = true;
            state.last_line = line;
            return std::nullopt;
        }

        const auto observed_stride = line - state.last_line;
        if (observed_stride == state.stride && observed_stride != 0) {
            state.confidence = static_cast<std::uint8_t>(std::min<int>(3, state.confidence + 1));
        } else {
            state.stride = observed_stride;
            state.confidence = 0;
        }
        state.last_line = line;

        if (state.confidence >= 2) {
            const auto predicted_line = line + state.stride;
            if (predicted_line >= 0) {
                return static_cast<std::uint64_t>(predicted_line) * line_size_;
            }
        }
        return std::nullopt;
    }

    PrefetcherKind kind_;
    std::size_t line_size_;
    State global_;
    std::unordered_map<std::uint64_t, State> by_pc_;
};

class Simulator {
public:
    Simulator(CacheConfig l1_config,
              CacheConfig l2_config,
              Protocol protocol,
              PrefetcherKind prefetcher,
              FutureUseTable* future)
        : l1_("L1", l1_config, future),
          l2_("L2", l2_config, future),
          protocol_(protocol),
          line_size_(l1_config.line_size),
          prefetcher_(prefetcher, l1_config.line_size) {}

    const Stats& run(const std::vector<Access>& trace, FutureUseTable& future) {
        for (std::size_t index = 0; index < trace.size(); ++index) {
            const auto& access = trace[index];
            future.consume(access.address / line_size(), index);
            demand_access(access, index + 1);
            if (const auto predicted = prefetcher_.observe(access)) {
                prefetch(*predicted, index + 1);
            }
        }
        return stats_;
    }

private:
    std::size_t line_size() const {
        return line_size_;
    }

    void demand_access(const Access& access, std::size_t cycle) {
        ++stats_.demand_accesses;
        if (access.op == 'W') {
            ++stats_.write_accesses;
        }

        const auto l1 = l1_.access(access.address, cycle);
        if (l1.hit) {
            ++stats_.l1_hits;
            stats_.useful_prefetches += l1.useful_prefetch ? 1 : 0;
            return;
        }

        ++stats_.l1_misses;
        switch (protocol_) {
        case Protocol::Inclusive:
            access_inclusive(access.address, cycle, false);
            break;
        case Protocol::Exclusive:
            access_exclusive(access.address, cycle, false);
            break;
        case Protocol::Nine:
            access_nine(access.address, cycle, false);
            break;
        case Protocol::Hybrid:
            access_hybrid(access.address, cycle, false);
            break;
        }
    }

    void prefetch(std::uint64_t address, std::size_t cycle) {
        if (l1_.contains(address) || l2_.contains(address)) {
            ++stats_.prefetch_hits;
            return;
        }
        ++stats_.prefetches_issued;
        ++stats_.prefetch_memory_reads;
        switch (protocol_) {
        case Protocol::Inclusive:
            install_prefetch_inclusive(address, cycle);
            break;
        case Protocol::Exclusive:
            install_prefetch_exclusive(address, cycle);
            break;
        case Protocol::Nine:
            install_prefetch_nine(address, cycle);
            break;
        case Protocol::Hybrid:
            install_prefetch_hybrid(address, cycle);
            break;
        }
    }

    void access_inclusive(std::uint64_t address, std::size_t cycle, bool prefetched) {
        const auto l2 = l2_.access(address, cycle, prefetched);
        if (l2.hit) {
            ++stats_.l2_hits;
            stats_.useful_prefetches += l2.useful_prefetch ? 1 : 0;
        } else {
            ++stats_.l2_misses;
            ++stats_.memory_reads;
            auto l2_insert = l2_.insert(address, cycle, prefetched);
            if (l2_insert.evicted && l1_.invalidate_line_address(l2_insert.evicted->line_address)) {
                ++stats_.invalidations;
            }
        }

        auto l1_insert = l1_.insert(address, cycle, prefetched);
        if (l1_insert.evicted) {
            auto l2_insert = l2_.insert(l1_insert.evicted->line_address * line_size_, cycle, l1_insert.evicted->prefetched);
            if (l2_insert.evicted && l1_.invalidate_line_address(l2_insert.evicted->line_address)) {
                ++stats_.invalidations;
            }
        }
    }

    void access_exclusive(std::uint64_t address, std::size_t cycle, bool prefetched) {
        const auto l2 = l2_.access(address, cycle, prefetched);
        if (l2.hit) {
            ++stats_.l2_hits;
            stats_.useful_prefetches += l2.useful_prefetch ? 1 : 0;
            l2_.remove(address);
        } else {
            ++stats_.l2_misses;
            ++stats_.memory_reads;
        }

        auto l1_insert = l1_.insert(address, cycle, prefetched);
        if (l1_insert.evicted) {
            l2_.insert(l1_insert.evicted->line_address * line_size_, cycle, l1_insert.evicted->prefetched);
        }
    }

    void access_nine(std::uint64_t address, std::size_t cycle, bool prefetched) {
        const auto l2 = l2_.access(address, cycle, prefetched);
        if (l2.hit) {
            ++stats_.l2_hits;
            stats_.useful_prefetches += l2.useful_prefetch ? 1 : 0;
        } else {
            ++stats_.l2_misses;
            ++stats_.memory_reads;
            l2_.insert(address, cycle, prefetched);
        }

        l1_.insert(address, cycle, prefetched);
    }

    void access_hybrid(std::uint64_t address, std::size_t cycle, bool prefetched) {
        const auto l2 = l2_.access(address, cycle, prefetched);
        if (l2.hit) {
            ++stats_.l2_hits;
            stats_.useful_prefetches += l2.useful_prefetch ? 1 : 0;
        } else {
            ++stats_.l2_misses;
            ++stats_.memory_reads;
        }

        auto l1_insert = l1_.insert(address, cycle, prefetched);
        if (l1_insert.evicted) {
            l2_.insert(l1_insert.evicted->line_address * line_size_, cycle, l1_insert.evicted->prefetched);
        }
        if (!l2.hit && prefetched) {
            l2_.insert(address, cycle, true);
        }
    }

    void install_prefetch_inclusive(std::uint64_t address, std::size_t cycle) {
        auto l2_insert = l2_.insert(address, cycle, true);
        if (l2_insert.evicted && l1_.invalidate_line_address(l2_insert.evicted->line_address)) {
            ++stats_.invalidations;
        }
        auto l1_insert = l1_.insert(address, cycle, true);
        if (l1_insert.evicted) {
            auto victim_insert = l2_.insert(l1_insert.evicted->line_address * line_size_, cycle, l1_insert.evicted->prefetched);
            if (victim_insert.evicted && l1_.invalidate_line_address(victim_insert.evicted->line_address)) {
                ++stats_.invalidations;
            }
        }
    }

    void install_prefetch_exclusive(std::uint64_t address, std::size_t cycle) {
        auto l1_insert = l1_.insert(address, cycle, true);
        if (l1_insert.evicted) {
            l2_.insert(l1_insert.evicted->line_address * line_size_, cycle, l1_insert.evicted->prefetched);
        }
    }

    void install_prefetch_nine(std::uint64_t address, std::size_t cycle) {
        l2_.insert(address, cycle, true);
        l1_.insert(address, cycle, true);
    }

    void install_prefetch_hybrid(std::uint64_t address, std::size_t cycle) {
        l2_.insert(address, cycle, true);
        auto l1_insert = l1_.insert(address, cycle, true);
        if (l1_insert.evicted) {
            l2_.insert(l1_insert.evicted->line_address * line_size_, cycle, l1_insert.evicted->prefetched);
        }
    }

    Cache l1_;
    Cache l2_;
    Protocol protocol_;
    std::size_t line_size_;
    Prefetcher prefetcher_;
    Stats stats_;
};

void print_usage(const char* binary) {
    std::cout << "Usage: " << binary << " --trace data/sample.trace [options]\n\n"
              << "Options:\n"
              << "  --policy lru|quad-age|belady\n"
              << "  --protocol inclusive|exclusive|nine|hybrid\n"
              << "  --prefetch none|pc-stride|addr-stride\n"
              << "  --l1-size bytes --l2-size bytes --line-size bytes\n"
              << "  --l1-assoc ways --l2-assoc ways\n";
}

double ratio(std::uint64_t numerator, std::uint64_t denominator) {
    if (denominator == 0) {
        return 0.0;
    }
    return static_cast<double>(numerator) / static_cast<double>(denominator);
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (has_flag(argc, argv, "--help") || argc == 1) {
            print_usage(argv[0]);
            return argc == 1 ? 1 : 0;
        }

        CacheConfig l1;
        CacheConfig l2;
        l1.size_bytes = parse_u64(arg_value(argc, argv, "--l1-size", "32768"));
        l2.size_bytes = parse_u64(arg_value(argc, argv, "--l2-size", "262144"));
        l1.line_size = parse_u64(arg_value(argc, argv, "--line-size", "64"));
        l2.line_size = l1.line_size;
        l1.associativity = parse_u64(arg_value(argc, argv, "--l1-assoc", "8"));
        l2.associativity = parse_u64(arg_value(argc, argv, "--l2-assoc", "8"));
        l1.policy = parse_policy(arg_value(argc, argv, "--policy", "lru"));
        l2.policy = l1.policy;

        const auto protocol = parse_protocol(arg_value(argc, argv, "--protocol", "inclusive"));
        const auto prefetcher = parse_prefetcher(arg_value(argc, argv, "--prefetch", "none"));
        const auto trace_path = arg_value(argc, argv, "--trace", "");
        if (trace_path.empty()) {
            throw std::invalid_argument("--trace is required");
        }

        auto trace = load_trace(trace_path);
        FutureUseTable future(trace, l1.line_size);
        Simulator simulator(l1, l2, protocol, prefetcher, &future);
        const auto& stats = simulator.run(trace, future);

        std::cout << std::fixed << std::setprecision(4);
        std::cout << "cache_sim_summary\n";
        std::cout << "  trace: " << trace_path << '\n';
        std::cout << "  replacement_policy: " << policy_name(l1.policy) << '\n';
        std::cout << "  protocol: " << protocol_name(protocol) << '\n';
        std::cout << "  prefetcher: " << prefetcher_name(prefetcher) << '\n';
        std::cout << "  demand_accesses: " << stats.demand_accesses << '\n';
        std::cout << "  write_accesses: " << stats.write_accesses << '\n';
        std::cout << "  l1_hits: " << stats.l1_hits << '\n';
        std::cout << "  l1_misses: " << stats.l1_misses << '\n';
        std::cout << "  l1_miss_rate: " << ratio(stats.l1_misses, stats.demand_accesses) << '\n';
        std::cout << "  l2_hits: " << stats.l2_hits << '\n';
        std::cout << "  l2_misses: " << stats.l2_misses << '\n';
        std::cout << "  l2_miss_rate_on_l1_miss: " << ratio(stats.l2_misses, stats.l1_misses) << '\n';
        std::cout << "  memory_reads: " << stats.memory_reads << '\n';
        std::cout << "  prefetches_issued: " << stats.prefetches_issued << '\n';
        std::cout << "  prefetch_hits: " << stats.prefetch_hits << '\n';
        std::cout << "  prefetch_memory_reads: " << stats.prefetch_memory_reads << '\n';
        std::cout << "  useful_prefetches: " << stats.useful_prefetches << '\n';
        std::cout << "  inclusive_invalidations: " << stats.invalidations << '\n';
    } catch (const std::exception& error) {
        std::cerr << "cache_sim error: " << error.what() << '\n';
        return 2;
    }
}
