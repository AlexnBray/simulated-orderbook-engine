#include "pch.h"
#include "scenario.h"

#include <cctype>
#include <type_traits>

namespace {

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool parseSide(const std::string& tok, Side& out) {
    const std::string u = upper(tok);
    if (u == "BID" || u == "BUY") { out = Side::Bid; return true; }
    if (u == "ASK" || u == "SELL") { out = Side::Ask; return true; }
    return false;
}

template <typename T>
bool parseNumber(const std::string& tok, T& out) {
    if (tok.empty()) return false;
    std::istringstream ss(tok);
    ss >> out;
    return ss && ss.peek() == std::char_traits<char>::eof() && !(tok[0] == '-' && std::is_unsigned<T>::value);
}

} // namespace

std::optional<ScenarioCommand> parseScenarioLine(const std::string& rawLine, std::size_t lineNo, std::string* err) {
    if (err) err->clear();
    const std::string line = rawLine.substr(0, rawLine.find('#'));
    std::istringstream ss(line);
    std::vector<std::string> tok;
    for (std::string t; ss >> t;) tok.push_back(t);
    if (tok.empty()) return std::nullopt;

    auto bad = [&](const std::string& why) -> std::optional<ScenarioCommand> {
        if (err) *err = "line " + std::to_string(lineNo) + ": " + why + " -> '" + rawLine + "'";
        return std::nullopt;
    };

    ScenarioCommand cmd;
    cmd.line = lineNo;
    const std::string kw = upper(tok[0]);

    if (kw == "FILL") return std::nullopt; // engine output, not an input
    if (kw == "LIMIT") {
        if (tok.size() != 4 && tok.size() != 5) return bad("expected LIMIT <BID|ASK> <price> <qty> [id]");
        cmd.type = ScenarioCommand::Type::Limit;
        if (!parseSide(tok[1], cmd.side)) return bad("bad side");
        if (!parseNumber(tok[2], cmd.price)) return bad("bad price");
        if (!parseNumber(tok[3], cmd.qty)) return bad("bad quantity");
        if (tok.size() == 5 && !parseNumber(tok[4], cmd.id)) return bad("bad id");
        return cmd;
    }
    if (kw == "MARKET") {
        if (tok.size() != 3) return bad("expected MARKET <BID|ASK> <qty>");
        cmd.type = ScenarioCommand::Type::Market;
        if (!parseSide(tok[1], cmd.side)) return bad("bad side");
        if (!parseNumber(tok[2], cmd.qty)) return bad("bad quantity");
        return cmd;
    }
    if (kw == "CANCEL") {
        if (tok.size() != 2) return bad("expected CANCEL <id>");
        cmd.type = ScenarioCommand::Type::Cancel;
        if (!parseNumber(tok[1], cmd.id)) return bad("bad id");
        return cmd;
    }
    if (kw == "MODIFY") {
        if (tok.size() != 4) return bad("expected MODIFY <id> <price> <qty>");
        cmd.type = ScenarioCommand::Type::Modify;
        if (!parseNumber(tok[1], cmd.id)) return bad("bad id");
        if (!parseNumber(tok[2], cmd.price)) return bad("bad price");
        if (!parseNumber(tok[3], cmd.qty)) return bad("bad quantity");
        return cmd;
    }
    return bad("unknown command '" + tok[0] + "'");
}

void applyCommand(const ScenarioCommand& cmd, OrderBook& book, ReplayResult& acc) {
    auto take = [&](const SubmitResult& r) {
        if (r.status == OrderStatus::Rejected) ++acc.rejected;
        acc.trades.insert(acc.trades.end(), r.trades.begin(), r.trades.end());
    };
    switch (cmd.type) {
        case ScenarioCommand::Type::Limit:
            take(book.insertLimit(Order{cmd.id, cmd.price, cmd.qty, cmd.side, 0}));
            break;
        case ScenarioCommand::Type::Market:
            take(book.insertMarket(cmd.side, cmd.qty));
            break;
        case ScenarioCommand::Type::Cancel:
            if (!book.cancel(cmd.id)) ++acc.rejected;
            break;
        case ScenarioCommand::Type::Modify:
            take(book.modify(cmd.id, cmd.price, cmd.qty));
            break;
    }
    ++acc.commandsApplied;
}

ReplayResult replayScenario(std::istream& in, OrderBook& book) {
    ReplayResult result;
    std::string line, err;
    std::size_t lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto cmd = parseScenarioLine(line, lineNo, &err);
        if (!cmd) {
            if (!err.empty()) result.errors.push_back(err);
            continue;
        }
        applyCommand(*cmd, book, result);
    }
    return result;
}
