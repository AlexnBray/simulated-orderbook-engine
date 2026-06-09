#include "pch.h"
#include <cassert>
#include "orderbook.h"

int main() {
    OrderBook book;

    auto mkOrder = [](OrderId id, Price price, Quantity qty, Side side) {
        return Order{id, price, qty, side, 0};
    };

    book.insertLimit(mkOrder(1, 100, 10, Side::Bid));
    assert(book.getBestBid() == 100);
    assert(book.getBestAsk() == 0);

    book.insertLimit(mkOrder(2, 105, 5, Side::Ask));
    assert(book.getBestBid() == 100);
    assert(book.getBestAsk() == 105);

    book.cancel(1);
    assert(book.getBestBid() == 0);
    assert(book.getBestAsk() == 105);

    book.insertLimit(mkOrder(3, 101, 7, Side::Bid));
    assert(book.getBestBid() == 101);

    book.insertMarket(Side::Ask, 3);
    assert(book.getBestBid() == 101);
    assert(book.getBestAsk() == 105);

    book.insertMarket(Side::Ask, 10);
    assert(book.getBestBid() == 0);
    assert(book.getBestAsk() == 105);

    book.cancel(2);
    assert(book.getBestAsk() == 0);

    book.insertLimit(mkOrder(4, 99, 4, Side::Bid));
    book.insertLimit(mkOrder(5, 105, 6, Side::Ask));
    book.printBook();

    std::cout << "All basic order-book checks passed.\n";
    return 0;
}