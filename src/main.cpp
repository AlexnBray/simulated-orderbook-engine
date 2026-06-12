#include "pch.h"
#include <cassert>

int main() {
    OrderBook book;

    auto mkOrder = [](OrderId id, Price price, Quantity qty, Side side) {
        return Order{id, price, qty, side, 0};
    };
    while (true) {
        std::string type;
        std::cout<<"Enter order type: ";
        if (!(std::cin >> type)) break;

    
        if (type == "limit") {
            Order order = createOrder();
            book.insertLimit(order);
        } else if (type == "market") {
            Order order = createMarket();
            book.insertMarket(order.side, order.qty);
        } else {
            std::cout << "Unknown type. Use limit or market.\n";
            continue;
        }

        book.printBook();
    }
}