#include "pch.h"
#pragma once
#include <string>
#include <ctime>



class OrderBook {
private:
    std::map<Price, PriceLevel> bids;
    std::map<Price, PriceLevel> asks;

    std::unordered_map<OrderId, OrderLocation> orderIndex;
    Price bestBid = 0;
    Price bestAsk = 0;

public:
    void insertLimit(Order order);
    void cancel(OrderId id);
    void insertMarket(Side side, Quantity qty);

    Price getBestBid() const { return bestBid; }
    Price getBestAsk() const { return bestAsk; }
    Price getSpread()  const { return bestAsk - bestBid; }
};