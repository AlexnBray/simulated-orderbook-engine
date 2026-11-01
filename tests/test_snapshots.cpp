// Roadmap 9) Top-of-Book / Depth Snapshots
#include <sstream>
#include "test_helpers.h"

namespace {
OrderBook sampleBook() {
    OrderBook b;
    b.insertLimit(bid(100, 5));
    b.insertLimit(bid(100, 3));
    b.insertLimit(bid(99, 4));
    b.insertLimit(bid(97, 1));
    b.insertLimit(ask(102, 6));
    b.insertLimit(ask(103, 2));
    b.insertLimit(ask(103, 2));
    return b;
}
} // namespace

TEST_CASE(l1_top_of_book) {
    const auto b = sampleBook();
    const auto t = b.topOfBook();
    REQUIRE(t.bestBid && t.bestAsk);
    CHECK_EQ(*t.bestBid, 100);
    CHECK_EQ(*t.bestAsk, 102);
    CHECK_EQ(t.bidQty, 8u);
    CHECK_EQ(t.askQty, 6u);
    CHECK_EQ(*t.spread(), 2);
    CHECK_EQ(b.getSpread(), 2);
}

TEST_CASE(l2_depth_aggregates_and_orders_levels) {
    const auto b = sampleBook();
    const auto bids = b.depth(Side::Bid, 10);
    REQUIRE(bids.size() == 3);
    CHECK((bids[0] == DepthLevel{100, 8, 2}));
    CHECK((bids[1] == DepthLevel{99, 4, 1}));
    CHECK((bids[2] == DepthLevel{97, 1, 1}));
    const auto asks = b.depth(Side::Ask, 10);
    REQUIRE(asks.size() == 2);
    CHECK((asks[0] == DepthLevel{102, 6, 1}));
    CHECK((asks[1] == DepthLevel{103, 4, 2}));
}

TEST_CASE(l2_depth_respects_level_limit) {
    const auto b = sampleBook();
    CHECK_EQ(b.depth(Side::Bid, 2).size(), 2u);
    CHECK_EQ(b.depth(Side::Bid, 0).size(), 0u);
}

TEST_CASE(l3_snapshot_in_priority_order) {
    const auto b = sampleBook();
    const auto s = b.snapshot();
    REQUIRE(s.bids.size() == 4);
    REQUIRE(s.asks.size() == 3);
    CHECK_EQ(s.bids[0].id, 1u);
    CHECK_EQ(s.bids[1].id, 2u);
    CHECK_EQ(s.bids[2].price, 99);
    CHECK_EQ(s.asks[0].price, 102);
    CHECK_EQ(s.asks[1].id, 6u);
    CHECK_EQ(s.asks[2].id, 7u);
}

TEST_CASE(snapshot_tracks_fills) {
    auto b = sampleBook();
    b.insertMarket(Side::Ask, 6); // eats id1 (5) and 1 of id2
    const auto s = b.snapshot();
    CHECK_EQ(s.bids[0].id, 2u);
    CHECK_EQ(s.bids[0].qty, 2u);
    CHECK_EQ(b.topOfBook().bidQty, 2u);
}

TEST_CASE(print_book_is_readable) {
    const auto b = sampleBook();
    std::ostringstream out;
    b.printBook(out);
    const std::string s = out.str();
    CHECK(s.find("BestBid: 100 | BestAsk: 102 | Spread: 2") != std::string::npos);
    CHECK(s.find("Price 103 | LevelQty 4") != std::string::npos);
}

TEST_MAIN()
