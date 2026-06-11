#include "pch.h"

OrderBook orderbook;

void Order::PartialUpdate(Quantity quantityfilled){
    //condition ? expression_if_true : expression_if_false;
    qty = (quantityfilled >= qty) ? 0 : (qty - quantityfilled);
    if (qty == 0){
        orderbook.cancel(id);
    }
}

//if quantity = 0, delete the order

void Order::modifyOrder(Price price, Quantity qty, Side side){

    return;
}


Order createOrder(){
    OrderId id;
    Quantity qty;
    Price price;
    std::string sideInput;
    Side side;

    while (true) {
        std::cout << "Type ID, Price, Quantity, side(bid/ask): ";
        if (!(std::cin >> id >> price >> qty >> sideInput)) {
            // Return a zeroed fallback order if input stream fails.
            return Order{0, 0, 0, Side::Bid, 0};
        }

        if (sideInput == "bid" || sideInput == "Bid") {
            side = Side::Bid;
            break;
        }
        if (sideInput == "ask" || sideInput == "Ask") {
            side = Side::Ask;
            break;
        }

        std::cout << "Invalid side. Use bid or ask.\n";
    }

    return Order{id, price, qty, side, 0};
}

Order createMarket(){
    OrderId id;
    Quantity qty;
    std::string sideInput;
    Side side;

    while (true) {
        std::cout << "Type ID, Quantity, side(bid/ask): ";
        if (!(std::cin >> id >> qty >> sideInput)) {
            // Return a zeroed fallback order if input stream fails.
            return Order{0, 0, 0, Side::Bid, 0};
        }

        if (sideInput == "bid" || sideInput == "Bid") {
            side = Side::Bid;
            break;
        }
        if (sideInput == "ask" || sideInput == "Ask") {
            side = Side::Ask;
            break;
        }

        std::cout << "Invalid side. Use bid or ask.\n";
    }

    return Order{id, 0, qty, side, 0};
}
//streamlit