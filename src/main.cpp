#include "pch.h"
#include "polymarketdata.h"
#include "scenario.h"

namespace {

void usage(const char* prog) {
    std::cout << "Usage:\n"
              << "  " << prog << "                                   interactive mode (limit/market from stdin)\n"
              << "  " << prog << " replay <scenario.txt> [--log out]  run a scenario / event log, print final book\n"
              << "  " << prog << " ingest <fills.csv> [--trades out]  ingest Polymarket OrderFilled events\n";
}

// returns the value following `flag`, or "" if absent
std::string flagValue(int argc, char** argv, const std::string& flag) {
    for (int i = 2; i + 1 < argc; ++i)
        if (flag == argv[i]) return argv[i + 1];
    return "";
}

int runInteractive() {
    OrderBook book;
    std::ofstream log("logfiles/log1.txt", std::ios::app); // trade output; ignored if the folder is missing
    book.setTradeListener([&](const Trade& t) {
        std::cout << "TRADE price=" << t.price << " qty=" << t.qty << " maker=" << t.makerId
                  << " taker=" << t.takerId << '\n';
        if (log) log << t.sequence << ',' << t.price << ',' << t.qty << ',' << t.makerId << ',' << t.takerId << '\n';
    });

    while (true) {
        std::string type;
        std::cout << "Enter order type (limit/market/cancel/quit): ";
        if (!(std::cin >> type) || type == "quit") break;

        if (type == "limit") {
            const auto r = book.insertLimit(createOrder());
            std::cout << "order " << r.id << " -> " << toString(r.status) << ' ' << r.reason << '\n';
        } else if (type == "market") {
            const Order order = createMarket();
            const auto r = book.insertMarket(order.side, order.qty);
            std::cout << "market -> " << toString(r.status) << " filled " << r.filledQty << '\n';
        } else if (type == "cancel") {
            OrderId id = 0;
            if (!(std::cin >> id)) break;
            std::cout << (book.cancel(id) ? "cancelled\n" : "unknown id\n");
        } else {
            std::cout << "Unknown type. Use limit, market, cancel or quit.\n";
            continue;
        }
        book.printBook();
    }
    return 0;
}

int runReplay(int argc, char** argv) {
    std::ifstream in(argv[2]);
    if (!in) {
        std::cerr << "cannot open " << argv[2] << '\n';
        return 1;
    }
    OrderBook book;
    std::ofstream log;
    const std::string logPath = flagValue(argc, argv, "--log");
    if (!logPath.empty()) {
        log.open(logPath);
        book.setEventLog(&log);
    }
    const ReplayResult r = replayScenario(in, book);
    for (const auto& t : r.trades)
        std::cout << "TRADE #" << t.sequence << " price=" << t.price << " qty=" << t.qty << " maker=" << t.makerId
                  << " taker=" << t.takerId << '\n';
    book.printBook();
    std::cout << "commands=" << r.commandsApplied << " rejected=" << r.rejected << " trades=" << r.trades.size()
              << '\n';
    for (const auto& e : r.errors) std::cerr << e << '\n';
    std::string err;
    if (!book.checkInvariants(&err)) {
        std::cerr << "INVARIANT VIOLATION: " << err << '\n';
        return 2;
    }
    return r.errors.empty() ? 0 : 1;
}

int runIngest(int argc, char** argv) {
    std::ifstream in(argv[2]);
    if (!in) {
        std::cerr << "cannot open " << argv[2] << '\n';
        return 1;
    }
    polymarket::FillReplayer replayer;
    std::ofstream trades;
    const std::string tradesPath = flagValue(argc, argv, "--trades");
    if (!tradesPath.empty()) {
        trades.open(tradesPath);
        trades << "timestamp,asset_id,taker_side,price,qty,engine_maker_id,engine_taker_id,maker,taker\n";
        replayer.setTradeLog(&trades);
    }
    const auto stats = polymarket::ingestCsv(in, replayer);
    polymarket::printSummary(std::cout, stats, replayer);
    return stats.engineMismatches == 0 && stats.eventsParsed > 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 1) return runInteractive();
    const std::string mode = argv[1];
    if (mode == "replay" && argc >= 3) return runReplay(argc, argv);
    if (mode == "ingest" && argc >= 3) return runIngest(argc, argv);
    usage(argv[0]);
    return mode == "--help" || mode == "-h" ? 0 : 1;
}
