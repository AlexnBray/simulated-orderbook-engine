#pragma once
#include <ostream>
#include <string>
#include "orderbook.h"
#include "test_framework.h"

inline std::ostream& operator<<(std::ostream& o, OrderStatus s) { return o << toString(s); }
inline std::ostream& operator<<(std::ostream& o, Side s) { return o << toString(s); }

// Assert the book's structural invariants (index/levels/aggregates/top-of-book) hold.
#define CHECK_INVARIANTS(book)                                                        \
    do {                                                                              \
        std::string tf_err;                                                           \
        if (!(book).checkInvariants(&tf_err))                                         \
            ::tf::reportFailure(__FILE__, __LINE__, "invariant violated: " + tf_err); \
    } while (0)

inline Order limit(Side side, Price price, Quantity qty, OrderId id = 0) { return Order{id, price, qty, side, 0}; }
inline Order bid(Price price, Quantity qty, OrderId id = 0) { return limit(Side::Bid, price, qty, id); }
inline Order ask(Price price, Quantity qty, OrderId id = 0) { return limit(Side::Ask, price, qty, id); }
