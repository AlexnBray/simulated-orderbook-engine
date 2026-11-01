#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>
#include "orders.h"

enum class OrderStatus {
    Rejected,        // invalid input (zero qty, non-positive price, duplicate/unknown id)
    Resting,         // nothing filled, whole order rests on the book
    PartiallyFilled, // some quantity filled; limit remainder rests, market remainder is dropped
    Filled,          // fully executed
    Cancelled        // removed from the book (modify to qty 0)
};
const char* toString(OrderStatus s);

struct SubmitResult {
    OrderId id{0};
    OrderStatus status{OrderStatus::Rejected};
    Quantity filledQty{0};
    Quantity restingQty{0};
    std::vector<Trade> trades;
    std::string reason; // set when Rejected
};

// L1 view
struct TopOfBook {
    std::optional<Price> bestBid;
    std::optional<Price> bestAsk;
    Quantity bidQty{0};
    Quantity askQty{0};
    std::optional<Price> spread() const {
        if (bestBid && bestAsk) return *bestAsk - *bestBid;
        return std::nullopt;
    }
};

// L2 view: one aggregated price level
struct DepthLevel {
    Price price;
    Quantity qty;
    std::size_t orderCount;
    bool operator==(const DepthLevel& o) const {
        return price == o.price && qty == o.qty && orderCount == o.orderCount;
    }
};

// L3 view: every resting order, in priority order (bids high->low, asks low->high)
struct RestingOrder {
    OrderId id;
    Price price;
    Quantity qty;
    Side side;
    bool operator==(const RestingOrder& o) const {
        return id == o.id && price == o.price && qty == o.qty && side == o.side;
    }
};
struct BookSnapshot {
    std::vector<RestingOrder> bids;
    std::vector<RestingOrder> asks;
    bool operator==(const BookSnapshot& o) const { return bids == o.bids && asks == o.asks; }
    bool operator!=(const BookSnapshot& o) const { return !(*this == o); }
};

/*
Single-threaded, deterministic limit order book with price-time priority.

Policies:
  * IDs: id == 0 -> engine assigns the next id; a caller-supplied id that is
    already resting is rejected (duplicate ID policy).
  * Limit orders that cross match immediately at the resting (maker) price;
    any remainder rests at the limit price.
  * Market orders sweep the opposite side; any unfilled remainder is dropped.
  * modify(): reducing quantity at the same price keeps time priority. Any price
    change or quantity increase is a cancel/replace: the order keeps its id but
    goes to the back of the queue (and may cross).
*/
class OrderBook {
private:
    using LevelMap = std::map<Price, PriceLevel, std::less<Price>, PoolAllocator<std::pair<const Price, PriceLevel>>>;
    using OrderIndex = std::unordered_map<OrderId, OrderLocation, std::hash<OrderId>, std::equal_to<OrderId>,
                                          PoolAllocator<std::pair<const OrderId, OrderLocation>>>;

    LevelMap bids; // Key is the price of the order, PriceLevel references a struct which contains a list of orders in the price level.
    LevelMap asks;
    OrderIndex orderIndex;
    Price bestBid = 0; // 0 when that side is empty
    Price bestAsk = 0;
    OrderId nextOrderId{0};
    std::int64_t nextSequence{0};
    std::uint64_t nextTradeSeq{0};
    std::function<void(const Trade&)> tradeListener;
    std::ostream* eventLog{nullptr};

    OrderId generateOrderId();
    void fillOrder(PriceLevel& level, Quantity& remainingQuantity, OrderId takerId, Side takerSide,
                   std::vector<Trade>& trades);
    void refreshTopOfBook();
    Quantity matchAgainstBook(Side side, Quantity& remainingQty, bool isMarket, Price limitPrice,
                              OrderId takerId, std::vector<Trade>& trades);
    SubmitResult submitLimit(Order order);
    void restOrder(const Order& order);
    void removeResting(OrderIndex::iterator idxIt);
    void logEvent(const std::string& line);

public:
    // --- commands ---
    SubmitResult insertLimit(Order order);
    SubmitResult insertMarket(Side side, Quantity qty);
    bool cancel(OrderId id);
    SubmitResult modify(OrderId id, Price newPrice, Quantity newQty);

    // --- observers / hooks ---
    // Called synchronously for every trade, in execution order.
    void setTradeListener(std::function<void(const Trade&)> listener) { tradeListener = std::move(listener); }
    // Append-only event log of accepted commands and resulting fills (see scenario.h for format).
    void setEventLog(std::ostream* out) { eventLog = out; }

    // --- queries ---
    bool hasOrder(OrderId id) const { return orderIndex.find(id) != orderIndex.end(); }
    std::optional<Order> getOrder(OrderId id) const;
    std::size_t orderCount() const { return orderIndex.size(); }
    std::size_t levelCount(Side side) const { return side == Side::Bid ? bids.size() : asks.size(); }
    bool empty() const { return orderIndex.empty(); }

    Price getBestBid() const { return bestBid; }   // 0 when no bids
    Price getBestAsk() const { return bestAsk; }   // 0 when no asks
    Price getSpread()  const { return (bids.empty() || asks.empty()) ? 0 : bestAsk - bestBid; }

    TopOfBook topOfBook() const;                                    // L1
    std::vector<DepthLevel> depth(Side side, std::size_t maxLevels) const; // L2
    BookSnapshot snapshot() const;                                  // L3
    Quantity totalQty(Side side) const;

    // Verifies every structural invariant. Returns true when consistent;
    // otherwise false and (if err != nullptr) a description of the first violation.
    bool checkInvariants(std::string* err = nullptr) const;

    void printBook(std::ostream& out) const;
    void printBook() const;
};
