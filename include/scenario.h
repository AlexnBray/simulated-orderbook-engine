#pragma once
#include <cstddef>
#include <istream>
#include <optional>
#include <string>
#include <vector>
#include "orderbook.h"

/*
Plain-text command format shared by scenario files and the engine's event log
(OrderBook::setEventLog). One command per line, case-insensitive keywords:

    LIMIT  <BID|ASK> <price> <qty> [id]
    MARKET <BID|ASK> <qty>
    CANCEL <id>
    MODIFY <id> <price> <qty>
    FILL   <price> <qty> <makerId> <takerId>   # written by the engine; ignored on replay

Blank lines and anything after '#' are ignored. Because the engine logs every
accepted command with its final id, replaying an event log into a fresh book
reproduces the original book exactly.
*/
struct ScenarioCommand {
    enum class Type { Limit, Market, Cancel, Modify };
    Type type{Type::Limit};
    Side side{Side::Bid};
    Price price{0};
    Quantity qty{0};
    OrderId id{0};
    std::size_t line{0};
};

// Returns std::nullopt for blank/comment/FILL lines and for malformed lines
// (in which case *err is set to a non-empty message).
std::optional<ScenarioCommand> parseScenarioLine(const std::string& line, std::size_t lineNo, std::string* err);

struct ReplayResult {
    std::size_t commandsApplied{0};
    std::size_t rejected{0};          // well-formed commands the engine refused
    std::vector<Trade> trades;
    std::vector<std::string> errors;  // malformed lines (with line numbers)
};

void applyCommand(const ScenarioCommand& cmd, OrderBook& book, ReplayResult& acc);
ReplayResult replayScenario(std::istream& in, OrderBook& book);
