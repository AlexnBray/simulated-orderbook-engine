#include "pch.h"
#include <cassert>

int main() {
    OrderBook book;

    auto mkOrder = [](OrderId id, Price price, Quantity qty, Side side) {
        return Order{id, price, qty, side, 0};
    };
    while (1){
        
        book.insertLimit(createOrder());
        book.printBook();
    }


    std::cout << "All basic order-book checks passed.\n";
    return 0;
}