#include "pch.h"


int main() {
    OrderBook book;

    while (true) {
        std::string type;
        std::cout<<"Enter order type: ";
        if (!(std::cin >> type)) break;

        
        if (type == "limit") {
            book.insertLimit(createOrder());
        } else if (type == "market") {
            Order order = createMarket();
            book.insertMarket(order.side, order.qty);
        } else {
            std::cout << "Unknown type. Use limit or market.\n";
            continue;
        }

        book.printBook();
    }
}//code refactor

