#include "pch.h"




OrderBook::insertLimitOrder(Order order){

    auto& loc = orderIndex[order.id];

    auto& map = (loc.side == Side::BUY) ? bids : asks;
    auto& level = map[loc.price];

}
    

    Price price;
    Side side;
    std::list<Order>::iterator iter;
    



/*
void cancel(OrderId id);
    void insertMarket(Side side, Quantity qty);

    Price getBestBid() const { return bestBid; }
    Price getBestAsk() const { return bestAsk; }
    Price getSpread()  const { return bestAsk - bestBid; }
    */