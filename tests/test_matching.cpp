// Roadmap 1) Correct Matching Engine Semantics
#include "test_helpers.h"

TEST_CASE(price_priority_best_ask_fills_first) {
    OrderBook b;
    b.insertLimit(ask(103, 5));
    const auto best = b.insertLimit(ask(101, 5));
    b.insertLimit(ask(102, 5));

    const auto r = b.insertMarket(Side::Bid, 5);
    REQUIRE(r.trades.size() == 1);
    CHECK_EQ(r.trades[0].price, 101);
    CHECK_EQ(r.trades[0].makerId, best.id);
    CHECK_EQ(b.getBestAsk(), 102);
    CHECK_INVARIANTS(b);
}

TEST_CASE(price_priority_best_bid_fills_first) {
    OrderBook b;
    b.insertLimit(bid(98, 5));
    const auto best = b.insertLimit(bid(100, 5));
    b.insertLimit(bid(99, 5));

    const auto r = b.insertMarket(Side::Ask, 5);
    REQUIRE(r.trades.size() == 1);
    CHECK_EQ(r.trades[0].price, 100);
    CHECK_EQ(r.trades[0].makerId, best.id);
    CHECK_EQ(b.getBestBid(), 99);
    CHECK_INVARIANTS(b);
}

TEST_CASE(time_priority_fifo_within_level) {
    OrderBook b;
    const auto first = b.insertLimit(ask(100, 3));
    const auto second = b.insertLimit(ask(100, 3));
    const auto third = b.insertLimit(ask(100, 3));

    const auto r = b.insertMarket(Side::Bid, 7);
    REQUIRE(r.trades.size() == 3);
    CHECK_EQ(r.trades[0].makerId, first.id);
    CHECK_EQ(r.trades[1].makerId, second.id);
    CHECK_EQ(r.trades[2].makerId, third.id);
    CHECK_EQ(r.trades[2].qty, 1u);
    // third order keeps its place with the remainder
    REQUIRE(b.getOrder(third.id).has_value());
    CHECK_EQ(b.getOrder(third.id)->qty, 2u);
    CHECK_INVARIANTS(b);
}

TEST_CASE(market_order_consumes_opposite_book) {
    OrderBook b;
    b.insertLimit(ask(100, 4));
    b.insertLimit(ask(101, 4));
    const auto r = b.insertMarket(Side::Bid, 6);
    CHECK_EQ(r.status, OrderStatus::Filled);
    CHECK_EQ(r.filledQty, 6u);
    CHECK_EQ(r.restingQty, 0u);
    CHECK_EQ(b.totalQty(Side::Ask), 2u);
    CHECK_EQ(b.totalQty(Side::Bid), 0u); // market orders never rest
    CHECK_INVARIANTS(b);
}

TEST_CASE(market_order_partial_when_book_too_thin) {
    OrderBook b;
    b.insertLimit(bid(100, 4));
    const auto r = b.insertMarket(Side::Ask, 10);
    CHECK_EQ(r.status, OrderStatus::PartiallyFilled);
    CHECK_EQ(r.filledQty, 4u);
    CHECK_EQ(r.restingQty, 0u); // remainder is dropped, not rested
    CHECK(b.empty());
    CHECK_INVARIANTS(b);
}

TEST_CASE(crossing_limit_matches_then_remainder_rests) {
    OrderBook b;
    b.insertLimit(ask(100, 5));
    b.insertLimit(ask(101, 5));
    b.insertLimit(ask(105, 5));

    const auto r = b.insertLimit(bid(102, 12));
    CHECK_EQ(r.status, OrderStatus::PartiallyFilled);
    CHECK_EQ(r.filledQty, 10u);
    CHECK_EQ(r.restingQty, 2u);
    REQUIRE(r.trades.size() == 2);
    CHECK_EQ(r.trades[0].price, 100); // trades print at maker price, not 102
    CHECK_EQ(r.trades[1].price, 101);
    CHECK_EQ(b.getBestBid(), 102);
    CHECK_EQ(b.getBestAsk(), 105);
    CHECK_INVARIANTS(b);
}

TEST_CASE(crossing_limit_respects_limit_price) {
    OrderBook b;
    b.insertLimit(bid(100, 5));
    b.insertLimit(bid(99, 5));
    const auto r = b.insertLimit(ask(100, 8)); // may only hit bids >= 100
    CHECK_EQ(r.filledQty, 5u);
    CHECK_EQ(r.restingQty, 3u);
    CHECK_EQ(b.getBestAsk(), 100);
    CHECK_EQ(b.getBestBid(), 99);
    CHECK_INVARIANTS(b);
}

TEST_CASE(non_crossing_limit_rests_untouched) {
    OrderBook b;
    b.insertLimit(ask(101, 5));
    const auto r = b.insertLimit(bid(100, 5));
    CHECK_EQ(r.status, OrderStatus::Resting);
    CHECK(r.trades.empty());
    CHECK_EQ(b.orderCount(), 2u);
    CHECK_INVARIANTS(b);
}

TEST_CASE(partial_fill_reduces_resting_order) {
    OrderBook b;
    const auto maker = b.insertLimit(ask(100, 10));
    b.insertMarket(Side::Bid, 4);
    REQUIRE(b.getOrder(maker.id).has_value());
    CHECK_EQ(b.getOrder(maker.id)->qty, 6u);
    CHECK_EQ(b.depth(Side::Ask, 1)[0].qty, 6u);
    CHECK_INVARIANTS(b);
}

TEST_CASE(full_fill_removes_resting_order) {
    OrderBook b;
    const auto maker = b.insertLimit(ask(100, 10));
    const auto r = b.insertLimit(bid(100, 10));
    CHECK_EQ(r.status, OrderStatus::Filled);
    CHECK(!b.hasOrder(maker.id));
    CHECK(!b.hasOrder(r.id)); // fully filled taker never rests
    CHECK(b.empty());
    CHECK_INVARIANTS(b);
}

TEST_CASE(trade_listener_sees_every_trade_in_order) {
    OrderBook b;
    std::vector<Trade> seen;
    b.setTradeListener([&](const Trade& t) { seen.push_back(t); });
    b.insertLimit(ask(100, 2));
    b.insertLimit(ask(101, 2));
    b.insertMarket(Side::Bid, 4);
    REQUIRE(seen.size() == 2);
    CHECK_EQ(seen[0].price, 100);
    CHECK_EQ(seen[1].price, 101);
    CHECK(seen[0].sequence < seen[1].sequence);
    CHECK_EQ(seen[0].takerSide, Side::Bid);
}

TEST_CASE(deterministic_identical_runs_and_timestamps) {
    // Same commands -> same trades and same book, even when every caller timestamp is identical.
    auto run = [] {
        OrderBook b;
        std::vector<Trade> trades;
        b.setTradeListener([&](const Trade& t) { trades.push_back(t); });
        for (int i = 0; i < 5; ++i) b.insertLimit(Order{0, 100, 2, Side::Ask, /*timestamp*/ 42});
        b.insertMarket(Side::Bid, 7);
        return std::make_pair(trades, b.snapshot());
    };
    const auto a = run();
    const auto c = run();
    REQUIRE(a.first.size() == c.first.size());
    for (std::size_t i = 0; i < a.first.size(); ++i) {
        CHECK_EQ(a.first[i].makerId, c.first[i].makerId);
        CHECK_EQ(a.first[i].qty, c.first[i].qty);
    }
    CHECK(a.second == c.second);
    // arrival order decides ties: ids 1,2,3 consumed, 4 partially
    CHECK_EQ(a.first[0].makerId, 1u);
    CHECK_EQ(a.first[3].makerId, 4u);
    CHECK_EQ(a.first[3].qty, 1u);
}

TEST_MAIN()
