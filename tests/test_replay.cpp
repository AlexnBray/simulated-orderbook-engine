// Roadmap 7) Event Logging + Replay, and Roadmap 3) deterministic regression scenarios
#include <fstream>
#include <random>
#include <sstream>
#include "scenario.h"
#include "test_helpers.h"

#ifndef OBS_SOURCE_DIR
#define OBS_SOURCE_DIR "."
#endif

namespace {
std::string scenarioPath(const std::string& name) { return std::string(OBS_SOURCE_DIR) + "/scenarios/" + name; }
} // namespace

TEST_CASE(parse_valid_commands) {
    std::string err;
    auto c = parseScenarioLine("limit bid 100 5 42  # comment", 1, &err);
    REQUIRE(c.has_value());
    CHECK(c->type == ScenarioCommand::Type::Limit);
    CHECK_EQ(c->side, Side::Bid);
    CHECK_EQ(c->price, 100);
    CHECK_EQ(c->qty, 5u);
    CHECK_EQ(c->id, 42u);

    c = parseScenarioLine("MARKET SELL 7", 2, &err);
    REQUIRE(c.has_value());
    CHECK(c->type == ScenarioCommand::Type::Market);
    CHECK_EQ(c->side, Side::Ask);

    c = parseScenarioLine("MODIFY 3 101 9", 3, &err);
    REQUIRE(c.has_value());
    CHECK(c->type == ScenarioCommand::Type::Modify);
    CHECK_EQ(c->id, 3u);
    CHECK_EQ(c->qty, 9u);
}

TEST_CASE(parse_ignores_blank_comment_and_fill_lines) {
    std::string err;
    CHECK(!parseScenarioLine("", 1, &err).has_value());
    CHECK(err.empty());
    CHECK(!parseScenarioLine("   # just a comment", 1, &err).has_value());
    CHECK(err.empty());
    CHECK(!parseScenarioLine("FILL 100 5 1 2", 1, &err).has_value());
    CHECK(err.empty());
}

TEST_CASE(parse_reports_malformed_lines) {
    std::string err;
    CHECK(!parseScenarioLine("LIMIT UP 100 5", 4, &err).has_value());
    CHECK(err.find("line 4") != std::string::npos);
    CHECK(!parseScenarioLine("LIMIT BID abc 5", 1, &err).has_value());
    CHECK(!err.empty());
    CHECK(!parseScenarioLine("MARKET BID -5", 1, &err).has_value());
    CHECK(!err.empty());
    CHECK(!parseScenarioLine("TELEPORT 1", 1, &err).has_value());
    CHECK(!err.empty());
    CHECK(!parseScenarioLine("CANCEL", 1, &err).has_value());
    CHECK(!err.empty());
}

TEST_CASE(replay_collects_errors_and_rejections) {
    std::istringstream in("LIMIT BID 100 5\nBOGUS\nCANCEL 999\nLIMIT BID 100 0\n");
    OrderBook b;
    const auto r = replayScenario(in, b);
    CHECK_EQ(r.commandsApplied, 3u);
    CHECK_EQ(r.rejected, 2u);
    CHECK_EQ(r.errors.size(), 1u);
    CHECK_EQ(b.orderCount(), 1u);
}

TEST_CASE(event_log_is_append_only_and_records_fills) {
    std::ostringstream log;
    OrderBook b;
    b.setEventLog(&log);
    b.insertLimit(ask(100, 5));
    b.insertLimit(bid(100, 3));
    b.insertLimit(bid(0, 3)); // rejected: not logged
    b.cancel(1);
    b.cancel(1);              // failed cancel: not logged
    CHECK_EQ(log.str(), std::string("LIMIT ASK 100 5 1\nLIMIT BID 100 3 2\nFILL 100 3 1 2\nCANCEL 1\n"));
}

TEST_CASE(replaying_event_log_reproduces_identical_book) {
    std::ostringstream log;
    OrderBook original;
    original.setEventLog(&log);
    std::vector<Trade> originalTrades;
    original.setTradeListener([&](const Trade& t) { originalTrades.push_back(t); });

    std::mt19937 rng(7);
    for (int i = 0; i < 3000; ++i) {
        const Side side = (rng() & 1) ? Side::Bid : Side::Ask;
        switch (rng() % 10) {
            case 0: case 1: case 2: case 3: case 4:
                original.insertLimit(limit(side, 90 + static_cast<Price>(rng() % 21), 1 + rng() % 50));
                break;
            case 5: case 6:
                original.insertMarket(side, 1 + rng() % 40);
                break;
            case 7: case 8:
                original.cancel(1 + rng() % (i + 1));
                break;
            default:
                original.modify(1 + rng() % (i + 1), 90 + static_cast<Price>(rng() % 21), rng() % 50);
        }
    }

    std::istringstream in(log.str());
    OrderBook replayed;
    const auto r = replayScenario(in, replayed);
    CHECK(r.errors.empty());
    CHECK_EQ(r.rejected, 0u); // the log only holds accepted commands
    CHECK(replayed.snapshot() == original.snapshot());
    REQUIRE(r.trades.size() == originalTrades.size());
    for (std::size_t i = 0; i < r.trades.size(); ++i) {
        if (r.trades[i].makerId != originalTrades[i].makerId || r.trades[i].qty != originalTrades[i].qty ||
            r.trades[i].price != originalTrades[i].price) {
            ::tf::reportFailure(__FILE__, __LINE__, "trade " + std::to_string(i) + " differs on replay");
            break;
        }
    }
    CHECK_INVARIANTS(replayed);
}

TEST_CASE(regression_crossing_and_sweep) {
    std::ifstream in(scenarioPath("crossing_and_sweep.txt"));
    REQUIRE(in.good());
    OrderBook b;
    const auto r = replayScenario(in, b);
    CHECK(r.errors.empty());
    REQUIRE(r.trades.size() == 2);
    CHECK_EQ(r.trades[0].price, 100);
    CHECK_EQ(r.trades[0].makerId, 1u);
    CHECK_EQ(r.trades[1].price, 101);
    CHECK_EQ(r.trades[1].makerId, 2u);
    CHECK_EQ(b.getBestBid(), 102);
    CHECK_EQ(b.getOrder(4)->qty, 2u);
    CHECK_EQ(b.getBestAsk(), 105);
    CHECK_INVARIANTS(b);
}

TEST_CASE(regression_priority_and_modify) {
    std::ifstream in(scenarioPath("priority_and_modify.txt"));
    REQUIRE(in.good());
    OrderBook b;
    const auto r = replayScenario(in, b);
    CHECK(r.errors.empty());
    CHECK_EQ(r.rejected, 0u);
    REQUIRE(r.trades.size() == 2);
    CHECK_EQ(r.trades[0].makerId, 1u);
    CHECK_EQ(r.trades[0].qty, 4u);
    CHECK_EQ(r.trades[1].makerId, 3u);
    CHECK_EQ(r.trades[1].qty, 2u);
    const auto s = b.snapshot();
    REQUIRE(s.asks.size() == 2);
    CHECK((s.asks[0] == RestingOrder{3, 100, 3, Side::Ask}));
    CHECK((s.asks[1] == RestingOrder{2, 100, 6, Side::Ask}));
    REQUIRE(s.bids.size() == 1);
    CHECK((s.bids[0] == RestingOrder{4, 98, 10, Side::Bid}));
    CHECK_INVARIANTS(b);
}

TEST_CASE(regression_demo_session_runs_clean) {
    std::ifstream in(scenarioPath("demo_session.txt"));
    REQUIRE(in.good());
    OrderBook b;
    const auto r = replayScenario(in, b);
    CHECK(r.errors.empty());
    CHECK_EQ(r.rejected, 0u);
    CHECK_EQ(r.trades.size(), 4u);
    CHECK_EQ(b.getBestBid(), 100);
    CHECK_EQ(b.getBestAsk(), 102);
    CHECK_INVARIANTS(b);
}

TEST_MAIN()
