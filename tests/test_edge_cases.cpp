// Roadmap 3) Edge cases: empty book, unknown IDs, zero qty, multi-level sweeps
#include "test_helpers.h"

TEST_CASE(market_into_empty_book_does_nothing) {
    OrderBook b;
    const auto r = b.insertMarket(Side::Bid, 10);
    CHECK_EQ(r.status, OrderStatus::Rejected);
    CHECK_EQ(r.filledQty, 0u);
    CHECK(r.trades.empty());
    CHECK(b.empty());
    CHECK_INVARIANTS(b);
}

TEST_CASE(empty_book_queries) {
    OrderBook b;
    const auto tob = b.topOfBook();
    CHECK(!tob.bestBid.has_value());
    CHECK(!tob.bestAsk.has_value());
    CHECK(!tob.spread().has_value());
    CHECK_EQ(b.getSpread(), 0);
    CHECK(b.depth(Side::Bid, 10).empty());
    CHECK(b.snapshot() == BookSnapshot{});
    CHECK(!b.getOrder(1).has_value());
}

TEST_CASE(cancel_unknown_id_returns_false) {
    OrderBook b;
    CHECK(!b.cancel(12345));
    b.insertLimit(bid(100, 1));
    CHECK(!b.cancel(999));
    CHECK_EQ(b.orderCount(), 1u);
    CHECK_INVARIANTS(b);
}

TEST_CASE(cancel_twice_second_is_noop) {
    OrderBook b;
    const auto o = b.insertLimit(bid(100, 1));
    CHECK(b.cancel(o.id));
    CHECK(!b.cancel(o.id));
    CHECK_INVARIANTS(b);
}

TEST_CASE(zero_quantity_rejected) {
    OrderBook b;
    CHECK_EQ(b.insertLimit(bid(100, 0)).status, OrderStatus::Rejected);
    CHECK_EQ(b.insertMarket(Side::Ask, 0).status, OrderStatus::Rejected);
    CHECK(b.empty());
    CHECK_INVARIANTS(b);
}

TEST_CASE(non_positive_price_rejected) {
    OrderBook b;
    CHECK_EQ(b.insertLimit(bid(0, 5)).status, OrderStatus::Rejected);
    CHECK_EQ(b.insertLimit(ask(-3, 5)).status, OrderStatus::Rejected);
    CHECK(b.empty());
}

TEST_CASE(market_sweeps_multiple_levels) {
    OrderBook b;
    for (Price p = 100; p < 105; ++p) b.insertLimit(ask(p, 2));
    const auto r = b.insertMarket(Side::Bid, 7);
    REQUIRE(r.trades.size() == 4);
    CHECK_EQ(r.trades[0].price, 100);
    CHECK_EQ(r.trades[3].price, 103);
    CHECK_EQ(r.trades[3].qty, 1u);
    CHECK_EQ(b.levelCount(Side::Ask), 2u);
    CHECK_EQ(b.getBestAsk(), 103);
    CHECK_INVARIANTS(b);
}

TEST_CASE(limit_sweeps_entire_side_then_rests) {
    OrderBook b;
    b.insertLimit(bid(100, 3));
    b.insertLimit(bid(99, 3));
    const auto r = b.insertLimit(ask(90, 10));
    CHECK_EQ(r.filledQty, 6u);
    CHECK_EQ(r.restingQty, 4u);
    CHECK_EQ(b.levelCount(Side::Bid), 0u);
    CHECK_EQ(b.getBestAsk(), 90);
    CHECK_EQ(b.getBestBid(), 0);
    CHECK_INVARIANTS(b);
}

TEST_CASE(market_larger_than_entire_book) {
    OrderBook b;
    b.insertLimit(ask(100, 1));
    b.insertLimit(ask(200, 1));
    const auto r = b.insertMarket(Side::Bid, 1000);
    CHECK_EQ(r.filledQty, 2u);
    CHECK_EQ(r.status, OrderStatus::PartiallyFilled);
    CHECK(b.empty());
    CHECK_INVARIANTS(b);
}

TEST_CASE(large_quantities_do_not_overflow) {
    OrderBook b;
    const Quantity big = 5'000'000'000ULL; // > uint32 max
    b.insertLimit(ask(1, big));
    b.insertLimit(ask(1, big));
    CHECK_EQ(b.totalQty(Side::Ask), 2 * big);
    const auto r = b.insertMarket(Side::Bid, big + 1);
    CHECK_EQ(r.filledQty, big + 1);
    CHECK_EQ(b.totalQty(Side::Ask), big - 1);
    CHECK_INVARIANTS(b);
}

TEST_MAIN()
