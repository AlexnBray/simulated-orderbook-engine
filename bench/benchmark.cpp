// Throughput + latency benchmark for the matching engine.
//
//   ./orderbook_bench [numOps=1000000] [seed=42] [prefill=0] [--csv]
//
// Workload: 60% limit (roughly half cross), 15% market, 25% cancel of a random
// previously issued id, prices clustered within +/-20 ticks of a fixed mid.
// `prefill` rests that many extra orders deep in the book (outside the active
// band) before timing starts, to measure how latency scales with book size.
// Heap allocations are counted by replacing global operator new.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <vector>
#include "orderbook.h"

// ---- allocation counter ----
static std::atomic<unsigned long long> g_allocs{0};
void* operator new(std::size_t n) {
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {
using Clock = std::chrono::steady_clock;
constexpr Price kMid = 1'000'000;

struct Op {
    int kind; // 0 limit, 1 market, 2 cancel
    Side side;
    Price price;
    Quantity qty;
    OrderId cancelId;
};

std::vector<Op> makeWorkload(std::size_t n, unsigned seed, OrderId alreadyIssued) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> kind(0, 99), off(-20, 20), qty(1, 100);
    std::vector<Op> ops;
    ops.reserve(n);
    OrderId issued = alreadyIssued;
    for (std::size_t i = 0; i < n; ++i) {
        const int k = kind(rng);
        const Side side = (rng() & 1) ? Side::Bid : Side::Ask;
        if (k < 60) {
            ops.push_back({0, side, kMid + off(rng), static_cast<Quantity>(qty(rng)), 0});
            ++issued;
        } else if (k < 75) {
            ops.push_back({1, side, 0, static_cast<Quantity>(qty(rng)), 0});
        } else {
            ops.push_back({2, side, 0, 0, issued ? 1 + rng() % issued : 1});
        }
    }
    return ops;
}

// Rest `n` orders outside the +/-20 active band: bids below, asks above, spread over 2,000 ticks each side.
void prefillBook(OrderBook& book, std::size_t n, unsigned seed) {
    std::mt19937_64 rng(seed ^ 0x9e3779b97f4a7c15ULL);
    std::uniform_int_distribution<int> depth(21, 2020), qty(1, 100);
    for (std::size_t i = 0; i < n; ++i) {
        const Side side = (i & 1) ? Side::Bid : Side::Ask;
        const Price p = side == Side::Bid ? kMid - depth(rng) : kMid + depth(rng);
        book.insertLimit(Order{0, p, static_cast<Quantity>(qty(rng)), side, 0});
    }
}

double pct(std::vector<long long>& v, double p) {
    if (v.empty()) return 0;
    const std::size_t idx = std::min(v.size() - 1, static_cast<std::size_t>(p / 100.0 * v.size()));
    std::nth_element(v.begin(), v.begin() + static_cast<long>(idx), v.end());
    return static_cast<double>(v[idx]);
}
} // namespace

int main(int argc, char** argv) {
    bool csv = false;
    std::vector<const char*> pos;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--csv") == 0) csv = true;
        else pos.push_back(argv[i]);
    }
    const std::size_t n = pos.size() > 0 ? std::strtoull(pos[0], nullptr, 10) : 1'000'000;
    const unsigned seed = pos.size() > 1 ? static_cast<unsigned>(std::strtoul(pos[1], nullptr, 10)) : 42;
    const std::size_t prefill = pos.size() > 2 ? std::strtoull(pos[2], nullptr, 10) : 0;

    OrderBook book;
    prefillBook(book, prefill, seed);
    const auto ops = makeWorkload(n, seed, static_cast<OrderId>(prefill));

    std::vector<long long> lat[3];
    for (auto& v : lat) v.reserve(n);
    std::size_t trades = 0;

    const unsigned long long allocsBefore = g_allocs.load();
    const auto start = Clock::now();
    for (const auto& op : ops) {
        const auto t0 = Clock::now();
        switch (op.kind) {
            case 0: trades += book.insertLimit(Order{0, op.price, op.qty, op.side, 0}).trades.size(); break;
            case 1: trades += book.insertMarket(op.side, op.qty).trades.size(); break;
            default: book.cancel(op.cancelId);
        }
        const auto t1 = Clock::now();
        lat[op.kind].push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }
    const double secs = std::chrono::duration<double>(Clock::now() - start).count();
    const double allocsPerOp = static_cast<double>(g_allocs.load() - allocsBefore) / static_cast<double>(n);

    std::string err;
    const bool ok = book.checkInvariants(&err);

    const char* names[3] = {"limit", "market", "cancel"};
    if (csv) {
        // prefill,ops,secs,ops_per_sec,allocs_per_op,then p50,p99,p999 for limit,market,cancel
        std::printf("%zu,%zu,%.4f,%.0f,%.3f", prefill, n, secs, n / secs, allocsPerOp);
        for (auto& v : lat) std::printf(",%.0f,%.0f,%.0f", pct(v, 50), pct(v, 99), pct(v, 99.9));
        std::printf("\n");
    } else {
        std::printf("ops=%zu  prefill=%zu  time=%.3fs  throughput=%.0f ops/s  trades=%zu  resting=%zu  allocs/op=%.3f\n",
                    n, prefill, secs, n / secs, trades, book.orderCount(), allocsPerOp);
        std::printf("%-8s %10s %10s %10s %10s %10s\n", "op", "count", "p50(ns)", "p99(ns)", "p99.9(ns)", "max(ns)");
        for (int k = 0; k < 3; ++k) {
            auto& v = lat[k];
            const std::size_t count = v.size();
            const long long mx = v.empty() ? 0 : *std::max_element(v.begin(), v.end());
            const double p50 = pct(v, 50), p99 = pct(v, 99), p999 = pct(v, 99.9);
            std::printf("%-8s %10zu %10.0f %10.0f %10.0f %10lld\n", names[k], count, p50, p99, p999, mx);
        }
    }
    if (!ok) {
        std::printf("INVARIANT VIOLATION after benchmark: %s\n", err.c_str());
        return 1;
    }
    return 0;
}
