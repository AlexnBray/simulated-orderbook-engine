#include "pch.h"

const char* toString(OrderStatus s) {
    switch (s) {
        case OrderStatus::Rejected:        return "REJECTED";
        case OrderStatus::Resting:         return "RESTING";
        case OrderStatus::PartiallyFilled: return "PARTIALLY_FILLED";
        case OrderStatus::Filled:          return "FILLED";
        case OrderStatus::Cancelled:       return "CANCELLED";
    }
    return "UNKNOWN";
}

namespace {
SubmitResult reject(OrderId id, std::string reason) {
    SubmitResult r;
    r.id = id;
    r.status = OrderStatus::Rejected;
    r.reason = std::move(reason);
    return r;
}
} // namespace

/////////// Commands ///////////

SubmitResult OrderBook::insertLimit(Order order) {
    SubmitResult result = submitLimit(order);
    if (result.status != OrderStatus::Rejected) {
        // log with the final id so a replay reproduces identical ids
        logEvent(std::string("LIMIT ") + toString(order.side) + ' ' + std::to_string(order.price) + ' ' +
                 std::to_string(order.qty) + ' ' + std::to_string(result.id));
        for (const auto& t : result.trades) {
            logEvent("FILL " + std::to_string(t.price) + ' ' + std::to_string(t.qty) + ' ' +
                     std::to_string(t.makerId) + ' ' + std::to_string(t.takerId));
        }
    }
    return result;
}

SubmitResult OrderBook::submitLimit(Order order) {
    /*
    Picks what side to place the order on, matches anything it crosses, then
    finds/creates the price level for the remainder.
    ID policy:
    - If caller didn't provide an id (0), engine assigns one.
    - If caller provided one, enforce uniqueness.
    */
    if (order.qty == 0) return reject(order.id, "quantity must be > 0");
    if (order.price <= 0) return reject(order.id, "limit price must be > 0");

    if (order.id == 0) {
        order.id = generateOrderId();
    } else if (orderIndex.find(order.id) != orderIndex.end()) {
        return reject(order.id, "duplicate order id");
    } else if (order.id > nextOrderId) {
        nextOrderId = order.id; // generated ids never collide with caller-supplied ones
    }
    if (order.timestamp == 0) order.timestamp = ++nextSequence;

    SubmitResult result;
    result.id = order.id;

    Quantity remaining = order.qty;
    const bool crosses =
        (order.side == Side::Bid && !asks.empty() && order.price >= asks.begin()->first) ||
        (order.side == Side::Ask && !bids.empty() && order.price <= bids.rbegin()->first);
    if (crosses) {
        matchAgainstBook(order.side, remaining, false, order.price, order.id, result.trades);
    }
    result.filledQty = order.qty - remaining;

    if (remaining > 0) {
        order.qty = remaining;
        restOrder(order);
        result.restingQty = remaining;
    }
    result.status = (remaining == 0)          ? OrderStatus::Filled
                    : (result.filledQty == 0) ? OrderStatus::Resting
                                              : OrderStatus::PartiallyFilled;
    refreshTopOfBook();
    return result;
}

void OrderBook::restOrder(const Order& order) {
    auto& book = (order.side == Side::Bid) ? bids : asks; // reference to either Side::Bid or Side::Ask
    auto& level = book[order.price]; // if key exists returns exisitng price level reference, else creates a PriceLevel object then returns that.
    level.price = order.price;
    level.orders.push_back(order);
    level.totalQty += order.qty;

    auto iter = std::prev(level.orders.end()); // iter points to the order we just appended
    orderIndex[order.id] = OrderLocation{order.price, order.side, iter};
}

SubmitResult OrderBook::insertMarket(Side side, Quantity qty) {
    if (qty == 0) return reject(0, "quantity must be > 0");
    // Nothing to trade against: reject without consuming an id or touching the event log.
    if ((side == Side::Bid ? asks : bids).empty()) return reject(0, "no liquidity");

    SubmitResult result;
    result.id = generateOrderId();
    ++nextSequence;
    Quantity remaining = qty;
    matchAgainstBook(side, remaining, true, 0, result.id, result.trades);
    result.filledQty = qty - remaining;
    result.restingQty = 0; // market orders never rest; unfilled remainder is dropped
    // a non-empty opposite side always fills at least one unit
    result.status = (remaining == 0) ? OrderStatus::Filled : OrderStatus::PartiallyFilled;

    logEvent(std::string("MARKET ") + toString(side) + ' ' + std::to_string(qty));
    for (const auto& t : result.trades) {
        logEvent("FILL " + std::to_string(t.price) + ' ' + std::to_string(t.qty) + ' ' +
                 std::to_string(t.makerId) + ' ' + std::to_string(t.takerId));
    }
    return result;
}

Quantity OrderBook::matchAgainstBook(Side side, Quantity& remainingQty, bool isMarket, Price limitPrice,
                                     OrderId takerId, std::vector<Trade>& trades) {
    const Quantity initialRemaining = remainingQty;
    auto& book = (side == Side::Bid) ? asks : bids;

    while (remainingQty > 0 && !book.empty()) {
        // best opposite level: lowest ask for an incoming bid, highest bid for an incoming ask
        auto levelIter = (side == Side::Bid) ? book.begin() : std::prev(book.end());
        auto& level = levelIter->second;

        if (!isMarket) {
            if (side == Side::Bid && levelIter->first > limitPrice) break;
            if (side == Side::Ask && levelIter->first < limitPrice) break;
        }

        while (remainingQty > 0 && !level.orders.empty()) {
            fillOrder(level, remainingQty, takerId, side, trades);
        }

        if (level.orders.empty()) {
            book.erase(levelIter);
        }
    }
    refreshTopOfBook();
    return initialRemaining - remainingQty;
}

bool OrderBook::cancel(OrderId id) {
    // index iterator (points to it, must be converted)
    auto idxIt = orderIndex.find(id); // find gives an iterator to a map element and map elements are pairs
    if (idxIt == orderIndex.end()) return false;
    removeResting(idxIt);
    refreshTopOfBook();
    logEvent("CANCEL " + std::to_string(id));
    return true;
}

void OrderBook::removeResting(std::unordered_map<OrderId, OrderLocation>::iterator idxIt) {
    // Copy location so we can safely erase index entry early.
    const OrderLocation loc = idxIt->second;
    auto& book = (loc.side == Side::Bid) ? bids : asks; // what side it is
    auto levelIter = book.find(loc.price);
    assert(levelIter != book.end() && "index points at a missing price level");
    auto& level = levelIter->second;

    const Quantity removedQty = loc.iter->qty;
    assert(level.totalQty >= removedQty && "level aggregate underflow");
    orderIndex.erase(idxIt); // prevent stale iterator in index after erase
    level.orders.erase(loc.iter);
    level.totalQty -= removedQty;

    if (level.orders.empty()) {
        book.erase(levelIter);
    }
}

SubmitResult OrderBook::modify(OrderId id, Price newPrice, Quantity newQty) {
    auto idxIt = orderIndex.find(id);
    if (idxIt == orderIndex.end()) return reject(id, "unknown order id");
    if (newPrice <= 0) return reject(id, "limit price must be > 0");

    const OrderLocation loc = idxIt->second;
    Order& resting = *loc.iter;

    SubmitResult result;
    result.id = id;

    if (newQty == 0) {
        removeResting(idxIt);
        refreshTopOfBook();
        result.status = OrderStatus::Cancelled;
        logEvent("MODIFY " + std::to_string(id) + ' ' + std::to_string(newPrice) + " 0");
        return result;
    }

    if (newPrice == resting.price && newQty <= resting.qty) {
        // Pure size reduction: keep time priority, update in place.
        auto& level = ((loc.side == Side::Bid) ? bids : asks).at(loc.price);
        level.totalQty -= (resting.qty - newQty);
        resting.qty = newQty;
        result.status = OrderStatus::Resting;
        result.restingQty = newQty;
        logEvent("MODIFY " + std::to_string(id) + ' ' + std::to_string(newPrice) + ' ' + std::to_string(newQty));
        return result;
    }

    // Cancel/replace: same id, loses time priority, may cross the book.
    Order replacement = resting;
    replacement.price = newPrice;
    replacement.qty = newQty;
    replacement.timestamp = 0; // re-stamp: new arrival time
    removeResting(idxIt);
    result = submitLimit(replacement);
    logEvent("MODIFY " + std::to_string(id) + ' ' + std::to_string(newPrice) + ' ' + std::to_string(newQty));
    for (const auto& t : result.trades) {
        logEvent("FILL " + std::to_string(t.price) + ' ' + std::to_string(t.qty) + ' ' +
                 std::to_string(t.makerId) + ' ' + std::to_string(t.takerId));
    }
    return result;
}

/////////// Helpers ///////////

OrderId OrderBook::generateOrderId() {
    return ++nextOrderId;
}

void OrderBook::fillOrder(PriceLevel& level, Quantity& remainingQuantity, OrderId takerId, Side takerSide,
                          std::vector<Trade>& trades) {
    if (level.orders.empty() || remainingQuantity == 0) return;

    auto restingIter = level.orders.begin(); // oldest order at this price = highest time priority
    Order& restingOrder = *restingIter;
    const Quantity matched = std::min(remainingQuantity, restingOrder.qty);

    remainingQuantity -= matched;
    restingOrder.PartialUpdate(matched);
    assert(level.totalQty >= matched && "level aggregate underflow");
    level.totalQty -= matched;

    Trade trade{level.price, matched, restingOrder.id, takerId, takerSide, ++nextTradeSeq};
    trades.push_back(trade);
    if (tradeListener) tradeListener(trade);

    if (restingOrder.qty == 0) {
        orderIndex.erase(restingOrder.id);
        level.orders.erase(restingIter);
    }
}

void OrderBook::refreshTopOfBook() {
    bestBid = bids.empty() ? 0 : bids.rbegin()->first;
    bestAsk = asks.empty() ? 0 : asks.begin()->first;
}

void OrderBook::logEvent(const std::string& line) {
    if (eventLog) *eventLog << line << '\n';
}

/////////// Queries ///////////

std::optional<Order> OrderBook::getOrder(OrderId id) const {
    auto it = orderIndex.find(id);
    if (it == orderIndex.end()) return std::nullopt;
    return *it->second.iter;
}

TopOfBook OrderBook::topOfBook() const {
    TopOfBook tob;
    if (!bids.empty()) {
        tob.bestBid = bids.rbegin()->first;
        tob.bidQty = bids.rbegin()->second.totalQty;
    }
    if (!asks.empty()) {
        tob.bestAsk = asks.begin()->first;
        tob.askQty = asks.begin()->second.totalQty;
    }
    return tob;
}

std::vector<DepthLevel> OrderBook::depth(Side side, std::size_t maxLevels) const {
    std::vector<DepthLevel> out;
    auto emit = [&](const PriceLevel& lvl) {
        out.push_back(DepthLevel{lvl.price, lvl.totalQty, lvl.orders.size()});
    };
    if (side == Side::Bid) {
        for (auto it = bids.rbegin(); it != bids.rend() && out.size() < maxLevels; ++it) emit(it->second);
    } else {
        for (auto it = asks.begin(); it != asks.end() && out.size() < maxLevels; ++it) emit(it->second);
    }
    return out;
}

BookSnapshot OrderBook::snapshot() const {
    BookSnapshot snap;
    for (auto it = bids.rbegin(); it != bids.rend(); ++it)
        for (const auto& o : it->second.orders) snap.bids.push_back({o.id, o.price, o.qty, o.side});
    for (const auto& [price, level] : asks)
        for (const auto& o : level.orders) snap.asks.push_back({o.id, o.price, o.qty, o.side});
    return snap;
}

Quantity OrderBook::totalQty(Side side) const {
    Quantity total = 0;
    for (const auto& [price, level] : (side == Side::Bid ? bids : asks)) total += level.totalQty;
    return total;
}

bool OrderBook::checkInvariants(std::string* err) const {
    auto fail = [&](const std::string& msg) {
        if (err) *err = msg;
        return false;
    };

    std::size_t seen = 0;
    for (Side side : {Side::Bid, Side::Ask}) {
        const auto& book = (side == Side::Bid) ? bids : asks;
        for (const auto& [price, level] : book) {
            const std::string where = std::string(toString(side)) + " level " + std::to_string(price);
            if (level.orders.empty()) return fail(where + ": empty level not cleaned up");
            if (level.price != price) return fail(where + ": level.price does not match map key");
            Quantity sum = 0;
            for (auto it = level.orders.begin(); it != level.orders.end(); ++it) {
                const Order& o = *it;
                const std::string ow = where + " order " + std::to_string(o.id);
                if (o.qty == 0) return fail(ow + ": zero-quantity order resting");
                if (o.price != price) return fail(ow + ": order price != level price");
                if (o.side != side) return fail(ow + ": order on wrong side");
                auto idx = orderIndex.find(o.id);
                if (idx == orderIndex.end()) return fail(ow + ": missing from orderIndex");
                if (idx->second.price != price || idx->second.side != side || &*idx->second.iter != &o)
                    return fail(ow + ": orderIndex location is stale");
                sum += o.qty;
                ++seen;
            }
            if (sum != level.totalQty) return fail(where + ": totalQty != sum of order quantities");
        }
    }
    if (seen != orderIndex.size()) return fail("orderIndex has entries for orders not in the book");

    const Price expectBid = bids.empty() ? 0 : bids.rbegin()->first;
    const Price expectAsk = asks.empty() ? 0 : asks.begin()->first;
    if (bestBid != expectBid) return fail("cached bestBid is stale");
    if (bestAsk != expectAsk) return fail("cached bestAsk is stale");
    if (!bids.empty() && !asks.empty() && bestBid >= bestAsk) return fail("book is crossed");
    return true;
}

void OrderBook::printBook() const { printBook(std::cout); }

void OrderBook::printBook(std::ostream& out) const {
    out << "=== ORDER BOOK ===\n";
    out << "BestBid: " << bestBid << " | BestAsk: " << bestAsk << " | Spread: " << getSpread() << '\n';

    out << "--- Asks (low -> high) ---\n";
    if (asks.empty()) {
        out << "(empty)\n";
    } else {
        for (const auto& [price, level] : asks) {
            out << "Price " << price << " | LevelQty " << level.totalQty << '\n';
            for (const auto& order : level.orders) {
                out << "  id=" << order.id << " qty=" << order.qty << " side=Ask"
                    << " ts=" << order.timestamp << '\n';
            }
        }
    }

    out << "--- Bids (high -> low) ---\n";
    if (bids.empty()) {
        out << "(empty)\n";
    } else {
        for (auto it = bids.rbegin(); it != bids.rend(); ++it) {
            out << "Price " << it->first << " | LevelQty " << it->second.totalQty << '\n';
            for (const auto& order : it->second.orders) {
                out << "  id=" << order.id << " qty=" << order.qty << " side=Bid"
                    << " ts=" << order.timestamp << '\n';
            }
        }
    }
    out << "==================\n";
}
