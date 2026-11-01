#pragma once
#include <cstdint>
#include <list>

// type aliasing
using Price    = std::int64_t;
using OrderId  = std::uint64_t;
// 64-bit so real market data (e.g. Polymarket share amounts in 1e-6 base units)
// cannot overflow a single order.
using Quantity = std::uint64_t;

enum class Side { Bid, Ask };

inline Side opposite(Side s) { return s == Side::Bid ? Side::Ask : Side::Bid; }
inline const char* toString(Side s) { return s == Side::Bid ? "BID" : "ASK"; }

// represents one individual order
struct Order {
    OrderId id{0};          // 0 = let the engine assign one
    Price price{0};
    Quantity qty{0};        // remaining (open) quantity
    Side side{Side::Bid};
    std::int64_t timestamp{0}; // 0 = engine stamps an arrival sequence number

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
    Price price{0};
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
    std::list<Order>::iterator iter; // pointer like handle to a specific node in a list
};

// One execution between a resting (maker) order and an incoming (taker) order.
// Trades always print at the maker's price.
struct Trade {
    Price price;
    Quantity qty;
    OrderId makerId;
    OrderId takerId;
    Side takerSide;
    std::uint64_t sequence; // engine-wide monotonically increasing trade number
};

// Interactive (stdin) helpers used by the CLI.
Order createOrder();
Order createMarket();
