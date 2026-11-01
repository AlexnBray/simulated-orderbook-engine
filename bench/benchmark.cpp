// Throughput + latency benchmark for the matching engine.
//   ./orderbook_bench [numOps=1000000] [seed=42]
// Workload: 60% limit (about half of them cross), 15% market, 25% cancel of a random live id,
// prices clustered around a moving mid so the book stays a realistic depth.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
#include "orderbook.h"

namespace {
using Clock = std::chrono::steady_clock;

struct Op {
    int kind; // 0 limit, 1 market, 2 cancel
    Side side;
    Price price;
    Quantity qty;
    OrderId cancelId;
};

std::vector<Op> makeWorkload(std::size_t n, unsigned seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> kind(0, 99), off(-20, 20), qty(1, 100);
    std::vector<Op> ops;
    ops.reserve(n);
    OrderId issued = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const int k = kind(rng);
        const Side side = (rng() & 1) ? Side::Bid : Side::Ask;
        if (k < 60) {
            ops.push_back({0, side, 10'000 + off(rng), static_cast<Quantity>(qty(rng)), 0});
            ++issued;
        } else if (k < 75) {
            ops.push_back({1, side, 0, static_cast<Quantity>(qty(rng)), 0});
        } else {
            ops.push_back({2, side, 0, 0, issued ? 1 + rng() % issued : 1});
        }
    }
    return ops;
}

double pct(std::vector<long long>& v, double p) {
    if (v.empty()) return 0;
    const std::size_t idx = std::min(v.size() - 1, static_cast<std::size_t>(p / 100.0 * v.size()));
    std::nth_element(v.begin(), v.begin() + static_cast<long>(idx), v.end());
    return static_cast<double>(v[idx]);
}
} // namespace

int main(int argc, char** argv) {
    const std::size_t n = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1'000'000;
    const unsigned seed = argc > 2 ? static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10)) : 42;
    const auto ops = makeWorkload(n, seed);

    OrderBook book;
    std::vector<long long> lat[3];
    for (auto& v : lat) v.reserve(n);
    std::size_t trades = 0;

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

    std::string err;
    const bool ok = book.checkInvariants(&err);

    std::printf("ops=%zu  time=%.3fs  throughput=%.0f ops/s  trades=%zu  resting=%zu\n", n, secs, n / secs, trades,
                book.orderCount());
    const char* names[3] = {"limit ", "market", "cancel"};
    std::printf("%-8s %10s %10s %10s %10s %10s\n", "op", "count", "p50(ns)", "p99(ns)", "p99.9(ns)", "max(ns)");
    for (int k = 0; k < 3; ++k) {
        auto& v = lat[k];
        const std::size_t count = v.size();
        const double p50 = pct(v, 50), p99 = pct(v, 99), p999 = pct(v, 99.9);
        const long long mx = v.empty() ? 0 : *std::max_element(v.begin(), v.end());
        std::printf("%-8s %10zu %10.0f %10.0f %10.0f %10lld\n", names[k], count, p50, p99, p999, mx);
    }
    if (!ok) {
        std::printf("INVARIANT VIOLATION after benchmark: %s\n", err.c_str());
        return 1;
    }
    return 0;
}
