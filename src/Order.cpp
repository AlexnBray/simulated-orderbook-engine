#include "pch.h"

void Order::PartialUpdate(Quantity quantityfilled){
    qty = (quantityfilled >= qty) ? 0 : (qty - quantityfilled);
}

Order createOrder(){
    Quantity qty;
    Price price;
    std::string sideInput;
    Side side;

    while (true) {
        std::cout << "Type Price, Quantity, side(bid/ask): ";
        if (!(std::cin >> price >> qty >> sideInput)) {
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
    
    return Order{0, price, qty, side, 0};
}

Order createMarket(){
    Quantity qty;
    std::string sideInput;
    Side side;

    while (true) {
        std::cout << "Type Quantity, side(bid/ask): ";
        if (!(std::cin >> qty >> sideInput)) {
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

    return Order{0, 0, qty, side, 0};
}
