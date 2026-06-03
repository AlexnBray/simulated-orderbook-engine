#pragma once
#include <string>
#include <ctime>
#include <cstdint>

using Price = std::int64_t;
using OrderId  = std::uint64_t;
using Quantity = std::uint32_t;

enum class Side { Buy, Sell };


struct Order {
    OrderId id;
    Price price;
    Quantity qty;
    Side ask;
    std::int64_t timestamp;
};

struct PriceLevel {
    Price               price;
    std::list<Order>    orders;    // list not deque — stable iterators
    Quantity            totalQty;  // cached sum, update on every insert/cancel
};

/*
BUY / SELL
TYPE
PRICE
QUANTITY


TIMESTAMP

therefore, a change is allowing the user to choose which type of order they wish to place potentially. 
float


*/