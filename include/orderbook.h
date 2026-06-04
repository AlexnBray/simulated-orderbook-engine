#pragma once
#include <map>
#include <unordered_map>
#include "orders.h"




class OrderBook {
private:
    std::map<Price, PriceLevel> bids;
    std::map<Price, PriceLevel> asks;
    std::unordered_map<OrderId, OrderLocation> orderIndex;
    Price bestBid = std::numeric_limits<Price>::min();
    Price bestAsk = std::numeric_limits<Price>::min();
    Price spread = std::abs(bestAsk - bestBid);

public:
    void insertLimit(Order order);
    void cancel(OrderId id);
    void insertMarket(Side side, Quantity qty);

    Price getBestBid() const { return bestBid; }
    Price getBestAsk() const { return bestAsk; }
    Price getSpread()  const { return bestAsk - bestBid; }
};