#pragma once
#include <string>
#include <ctime>
#include <cstdint>

//type aliasing 
using Price = std::int64_t;
using OrderId  = std::uint64_t;
using Quantity = std::uint32_t;


enum class Side { Bid, Ask };

//represents one individual order
struct Order {
    OrderId id;
    Price price;
    Quantity qty;
    Side side;
    std::int64_t timestamp;

    // to update partially filled orders
    void PartialUpdate(Quantity quantityfilled);
};

// represents one price section in the order book.
/*
example, there may be many orders at each price level, to store them all
we .push_back them into the PriceLevel structs lists (orders) therefore it is similar to a queue, however
we use a list so that we can easily pull orders out of their respective pricelevel containers (for order cancellations)
*/
struct PriceLevel {
    Price price;
    std::list<Order> orders;
    Quantity totalQty{0}; // total quantity of volume in this price level container
};

/*
fast pointer like location for an order
price represnents the container where the order sits at
iterator to exact node in PriceLevel.orders
*/
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