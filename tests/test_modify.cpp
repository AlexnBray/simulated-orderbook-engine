// Roadmap 8) Order Modify/Replace
// Policy: same-price size reduction keeps time priority; price change or size
// increase is cancel/replace (same id, back of the queue, may cross).
#include "test_helpers.h"

TEST_CASE(reduce_quantity_keeps_priority) {
    OrderBook b;
    const auto first = b.insertLimit(ask(100, 10));
    const auto second = b.insertLimit(ask(100, 10));
    const auto r = b.modify(first.id, 100, 4);
    CHECK_EQ(r.status, OrderStatus::Resting);
    CHECK_EQ(b.depth(Side::Ask, 1)[0].qty, 14u);

    const auto m = b.insertMarket(Side::Bid, 4);
    REQUIRE(m.trades.size() == 1);
    CHECK_EQ(m.trades[0].makerId, first.id); // still first in line
    CHECK(b.hasOrder(second.id));
    CHECK_INVARIANTS(b);
}

TEST_CASE(increase_quantity_loses_priority) {
    OrderBook b;
    const auto first = b.insertLimit(ask(100, 5));
    const auto second = b.insertLimit(ask(100, 5));
    b.modify(first.id, 100, 8);
    CHECK_EQ(b.getOrder(first.id)->qty, 8u);
    const auto m = b.insertMarket(Side::Bid, 5);
    REQUIRE(m.trades.size() == 1);
    CHECK_EQ(m.trades[0].makerId, second.id);
    CHECK_INVARIANTS(b);
}

TEST_CASE(price_change_moves_level_and_loses_priority) {
    OrderBook b;
    const auto a = b.insertLimit(bid(99, 5));
    const auto c = b.insertLimit(bid(100, 5));
    b.modify(a.id, 100, 5);
    CHECK_EQ(b.levelCount(Side::Bid), 1u);
    const auto snap = b.snapshot();
    REQUIRE(snap.bids.size() == 2);
    CHECK_EQ(snap.bids[0].id, c.id);
    CHECK_EQ(snap.bids[1].id, a.id);
    CHECK_INVARIANTS(b);
}

TEST_CASE(price_change_that_crosses_executes) {
    OrderBook b;
    const auto restingAsk = b.insertLimit(ask(101, 3));
    const auto bidOrder = b.insertLimit(bid(99, 5));
    const auto r = b.modify(bidOrder.id, 101, 5);
    CHECK_EQ(r.status, OrderStatus::PartiallyFilled);
    REQUIRE(r.trades.size() == 1);
    CHECK_EQ(r.trades[0].makerId, restingAsk.id);
    CHECK_EQ(r.trades[0].takerId, bidOrder.id); // keeps its id through replace
    CHECK_EQ(b.getOrder(bidOrder.id)->qty, 2u);
    CHECK_EQ(b.getBestBid(), 101);
    CHECK_INVARIANTS(b);
}

TEST_CASE(modify_to_zero_cancels) {
    OrderBook b;
    const auto o = b.insertLimit(bid(100, 5));
    CHECK_EQ(b.modify(o.id, 100, 0).status, OrderStatus::Cancelled);
    CHECK(!b.hasOrder(o.id));
    CHECK(b.empty());
    CHECK_INVARIANTS(b);
}

TEST_CASE(modify_unknown_or_invalid_rejected) {
    OrderBook b;
    CHECK_EQ(b.modify(77, 100, 5).status, OrderStatus::Rejected);
    const auto o = b.insertLimit(bid(100, 5));
    const auto before = b.snapshot();
    CHECK_EQ(b.modify(o.id, 0, 5).status, OrderStatus::Rejected);
    CHECK(b.snapshot() == before);
    CHECK_INVARIANTS(b);
}

TEST_CASE(modify_same_values_is_noop_keeping_priority) {
    OrderBook b;
    const auto first = b.insertLimit(bid(100, 5));
    b.insertLimit(bid(100, 5));
    b.modify(first.id, 100, 5);
    CHECK_EQ(b.snapshot().bids[0].id, first.id);
}

TEST_MAIN()
