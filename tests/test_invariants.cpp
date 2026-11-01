// Roadmap 2) Invariant-Safe State Management
#include <algorithm>
#include <random>
#include "test_helpers.h"

TEST_CASE(index_levels_and_aggregates_consistent_after_each_op) {
    OrderBook b;
    b.insertLimit(bid(100, 5));
    CHECK_INVARIANTS(b);
    b.insertLimit(bid(100, 7));
    CHECK_INVARIANTS(b);
    b.insertLimit(ask(103, 4));
    CHECK_INVARIANTS(b);
    b.insertMarket(Side::Ask, 6);
    CHECK_INVARIANTS(b);
    CHECK_EQ(b.depth(Side::Bid, 1)[0].qty, 6u);
    b.cancel(2);
    CHECK_INVARIANTS(b);
    CHECK(b.depth(Side::Bid, 5).empty());
}

TEST_CASE(no_stale_iterator_cancel_after_partial_fill) {
    OrderBook b;
    const auto a1 = b.insertLimit(ask(100, 10));
    const auto a2 = b.insertLimit(ask(100, 10));
    b.insertMarket(Side::Bid, 15); // a1 gone, a2 partially filled (iterator must still be valid)
    CHECK(!b.hasOrder(a1.id));
    CHECK(!b.cancel(a1.id)); // erased order must not be reachable via the index
    CHECK(b.cancel(a2.id));
    CHECK(b.empty());
    CHECK_INVARIANTS(b);
}

TEST_CASE(no_stale_iterator_cancel_middle_of_queue) {
    OrderBook b;
    b.insertLimit(bid(100, 1));
    const auto mid = b.insertLimit(bid(100, 2));
    const auto last = b.insertLimit(bid(100, 3));
    CHECK(b.cancel(mid.id));
    CHECK_INVARIANTS(b);
    // the remaining nodes are still reachable/cancellable
    CHECK(b.cancel(last.id));
    CHECK_INVARIANTS(b);
    CHECK_EQ(b.orderCount(), 1u);
}

TEST_CASE(empty_levels_cleaned_up_after_fill_and_cancel) {
    OrderBook b;
    b.insertLimit(ask(100, 5));
    b.insertLimit(ask(101, 5));
    const auto c = b.insertLimit(ask(102, 5));
    b.insertMarket(Side::Bid, 5);
    CHECK_EQ(b.levelCount(Side::Ask), 2u);
    b.cancel(c.id);
    CHECK_EQ(b.levelCount(Side::Ask), 1u);
    CHECK_INVARIANTS(b);
}

TEST_CASE(best_bid_ask_refreshed_on_every_path) {
    OrderBook b;
    CHECK_EQ(b.getBestBid(), 0);
    CHECK_EQ(b.getBestAsk(), 0);
    const auto x = b.insertLimit(bid(99, 1));
    CHECK_EQ(b.getBestBid(), 99);
    b.insertLimit(ask(101, 1));
    CHECK_EQ(b.getBestAsk(), 101);
    CHECK_EQ(b.getSpread(), 2);
    b.cancel(x.id);
    CHECK_EQ(b.getBestBid(), 0);         // cancel path
    b.insertMarket(Side::Bid, 1);
    CHECK_EQ(b.getBestAsk(), 0);         // market path
    b.insertLimit(bid(100, 1));
    const auto y = b.insertLimit(bid(98, 1));
    b.modify(y.id, 105, 1);
    CHECK_EQ(b.getBestBid(), 105);       // modify path
    CHECK_INVARIANTS(b);
}

TEST_CASE(duplicate_id_is_rejected_and_book_unchanged) {
    OrderBook b;
    CHECK_EQ(b.insertLimit(bid(100, 5, 7)).status, OrderStatus::Resting);
    const auto before = b.snapshot();
    const auto dup = b.insertLimit(ask(100, 5, 7)); // would cross if accepted
    CHECK_EQ(dup.status, OrderStatus::Rejected);
    CHECK(dup.trades.empty());
    CHECK(b.snapshot() == before);
    CHECK_INVARIANTS(b);
}

TEST_CASE(generated_ids_never_collide_with_caller_ids) {
    OrderBook b;
    b.insertLimit(bid(100, 1, 5));
    const auto gen = b.insertLimit(bid(100, 1)); // engine-assigned
    CHECK(gen.id > 5u);
    CHECK_EQ(gen.status, OrderStatus::Resting);
    // a filled order's id can be reused once it no longer rests
    b.insertMarket(Side::Ask, 2);
    CHECK_EQ(b.insertLimit(bid(100, 1, 5)).status, OrderStatus::Resting);
}

// ---------------- randomized differential test ----------------
// A deliberately naive reference engine (linear scans). Any disagreement with
// the real engine, or any invariant violation, fails the test.
namespace {
struct RefOrder {
    OrderId id;
    Side side;
    Price price;
    Quantity qty;
    long seq;
};
struct RefBook {
    std::vector<RefOrder> orders;
    long seq = 0;
    OrderId nextId = 0;

    std::vector<std::pair<OrderId, Quantity>> match(Side side, Quantity& qty, bool market, Price px) {
        std::vector<std::pair<OrderId, Quantity>> fills;
        while (qty > 0) {
            int best = -1;
            for (int i = 0; i < static_cast<int>(orders.size()); ++i) {
                const auto& o = orders[i];
                if (o.side == side) continue;
                if (!market && side == Side::Bid && o.price > px) continue;
                if (!market && side == Side::Ask && o.price < px) continue;
                if (best < 0) { best = i; continue; }
                const auto& c = orders[best];
                const bool better = side == Side::Bid ? (o.price < c.price || (o.price == c.price && o.seq < c.seq))
                                                      : (o.price > c.price || (o.price == c.price && o.seq < c.seq));
                if (better) best = i;
            }
            if (best < 0) break;
            const Quantity m = std::min(qty, orders[best].qty);
            fills.push_back({orders[best].id, m});
            qty -= m;
            orders[best].qty -= m;
            if (orders[best].qty == 0) orders.erase(orders.begin() + best);
        }
        return fills;
    }
    std::vector<std::pair<OrderId, Quantity>> limit(Side side, Price px, Quantity qty) {
        const OrderId id = ++nextId;
        auto fills = match(side, qty, false, px);
        if (qty > 0) orders.push_back({id, side, px, qty, ++seq});
        return fills;
    }
    std::vector<std::pair<OrderId, Quantity>> market(Side side, Quantity qty) {
        const bool liquidity =
            std::any_of(orders.begin(), orders.end(), [&](const RefOrder& o) { return o.side != side; });
        if (!liquidity) return {}; // rejected: consumes no id
        ++nextId;
        return match(side, qty, true, 0);
    }
    bool cancel(OrderId id) {
        auto it = std::find_if(orders.begin(), orders.end(), [&](const RefOrder& o) { return o.id == id; });
        if (it == orders.end()) return false;
        orders.erase(it);
        return true;
    }
    BookSnapshot snapshot() const {
        auto sorted = orders;
        std::stable_sort(sorted.begin(), sorted.end(), [](const RefOrder& a, const RefOrder& b) {
            if (a.side != b.side) return a.side == Side::Bid;
            if (a.price != b.price) return a.side == Side::Bid ? a.price > b.price : a.price < b.price;
            return a.seq < b.seq;
        });
        BookSnapshot s;
        for (const auto& o : sorted) (o.side == Side::Bid ? s.bids : s.asks).push_back({o.id, o.price, o.qty, o.side});
        return s;
    }
};
} // namespace

TEST_CASE(randomized_ops_match_reference_model_and_keep_invariants) {
    std::mt19937_64 rng(20260601);
    OrderBook b;
    RefBook ref;
    std::uniform_int_distribution<int> op(0, 9), px(95, 105), qty(1, 20);

    for (int step = 0; step < 20000; ++step) {
        const int o = op(rng);
        const Side side = (rng() & 1) ? Side::Bid : Side::Ask;
        std::vector<std::pair<OrderId, Quantity>> expect;
        std::vector<Trade> got;
        if (o < 6) {
            const Price p = px(rng);
            const Quantity q = static_cast<Quantity>(qty(rng));
            expect = ref.limit(side, p, q);
            got = b.insertLimit(limit(side, p, q)).trades;
        } else if (o < 8) {
            const Quantity q = static_cast<Quantity>(qty(rng));
            expect = ref.market(side, q);
            got = b.insertMarket(side, q).trades;
        } else {
            const OrderId id = ref.nextId ? 1 + rng() % ref.nextId : 1;
            const bool r1 = ref.cancel(id);
            const bool r2 = b.cancel(id);
            if (r1 != r2) {
                CHECK_EQ(r1, r2);
                return;
            }
        }
        bool same = got.size() == expect.size();
        for (std::size_t i = 0; same && i < got.size(); ++i)
            same = got[i].makerId == expect[i].first && got[i].qty == expect[i].second;
        if (!same) {
            ::tf::reportFailure(__FILE__, __LINE__, "trades diverged from reference at step " + std::to_string(step));
            return;
        }
        std::string err;
        if (!b.checkInvariants(&err)) {
            ::tf::reportFailure(__FILE__, __LINE__, "step " + std::to_string(step) + ": " + err);
            return;
        }
        if (step % 100 == 0 && !(b.snapshot() == ref.snapshot())) {
            ::tf::reportFailure(__FILE__, __LINE__, "book diverged from reference at step " + std::to_string(step));
            return;
        }
    }
    CHECK(b.snapshot() == ref.snapshot());
}

TEST_MAIN()
