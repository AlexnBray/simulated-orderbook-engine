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

    void PartialUpdate(Quantity quantityfilled);
};

struct PriceLevel {
    Price price;
    std::list<Order> orders;
    Quantity totalQty;
};

struct OrderLocation {
    Price price;
    Side side;
    std::list<Order>::iterator iter;
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