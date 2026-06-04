#include "pch.h"

void OrderBook::insertLimit(Order order) {
    /*
    Picks what side to place the order on, then finds/creates the price level in that map
    */
    auto& book = (order.side == Side::Bid) ? bids : asks; // reference to either Side::Bid or Side::Ask
    auto& level = book[order.price]; // if key exists returns exisitng price level reference, else creates a PriceLevel object then returns that.

    level.price = order.price;
    level.orders.push_back(order);
    level.totalQty += order.qty;

    auto iter = level.orders.end(); //end points to the element after the last element
    --iter; // iter should now point to the last element
    orderIndex[order.id] = OrderLocation{order.price, order.side, iter}; //unordered_map

    bestBid = bids.empty() ? 0 : bids.rbegin()->first; //if bid book is empty, sets best bid to 0, otherwise takes highest bid price
    bestAsk = asks.empty() ? 0 : asks.begin()->first;
}

void OrderBook::cancel(OrderId id) {

    auto& place = orderIndex[id];
    auto& book = (place.side == Side::Bid) ? bids : asks;



}

