#include "pch.h"

void OrderBook::insertLimit(Order order) {
    auto& book = (order.ask == Side::Buy) ? bids : asks;
    auto& level = book[order.price];

    level.price = order.price;
    level.orders.push_back(order);
    level.totalQty += order.qty;

    auto iter = level.orders.end();
    --iter;
    orderIndex[order.id] = OrderLocation{order.price, order.ask, iter};

    bestBid = bids.empty() ? 0 : bids.rbegin()->first;
    bestAsk = asks.empty() ? 0 : asks.begin()->first;
}