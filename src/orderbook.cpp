#include "pch.h"

void OrderBook::insertLimit(Order order) {
    /*
    Picks what side to place the order on, then finds/creates the price level in that map
    */
    auto& book = (order.side == Side::Bid) ? bids : asks; // reference to either Side::Bid or Side::Ask
    auto& level = book[order.price]; // if key exists returns exisitng price level reference, else creates a PriceLevel object then returns that.


    level.price = order.price;
    level.orders.push_back(order);
    level.totalQty += order.qty;

    auto iter = level.orders.end(); //end points to the element after the last element
    --iter; // iter should now point to the last element

    // Duplicate ID policy on insert
    auto idxIt = orderIndex.find(order.id);
    if (idxIt != orderIndex.end()) {
        std::cout << "This order already exists" << '\n';
        return;
    }
    orderIndex[order.id] = OrderLocation{order.price, order.side, iter}; //unordered_map

    refreshTopOfBook();
}

/*
Fix the return nothing 
Duplicate ID policy on insert ####Done
Tighter underflow assertions for qty invariants
tests for cancel edge-cases

*/

void OrderBook::insertMarket(Side side, Quantity qty) {
    Quantity remainingQuantity = qty;
    auto& book = (side == Side::Bid) ? asks : bids; // market order consumes opposite side

    while (remainingQuantity > 0 && !book.empty()) {
        auto levelIter = (side == Side::Bid) ? book.begin() : std::prev(book.end());
        auto& level = levelIter->second;

        while (remainingQuantity > 0 && !level.orders.empty()) {
            fillOrder(level, remainingQuantity);
        }

        if (level.orders.empty()) {
            book.erase(levelIter);
        }
    }

    refreshTopOfBook();
}


void OrderBook::cancel(OrderId id) {

    // index iterator (points to it, must be converted)
    auto idxIt = orderIndex.find(id); // find gives an iterator to a map element and map elements are pairs
    if (idxIt == orderIndex.end()) return;

    auto& place = idxIt->second; // order location object, references second object | OrderLocation
    auto& book = (place.side == Side::Bid) ? bids : asks; // what side it is

    auto levelIter = book.find(place.price); // again gives an iterator
    if (levelIter == book.end()) return; // checks if it exists in the map, if not continue

    auto& level = levelIter->second; // maps it to the price level (second entry)
    if (place.iter == level.orders.end()) return;

    const Quantity removedQty = place.iter->qty; // iterator pointing to one order node
    // this points to the node, then accesses the order's quantity field
    level.orders.erase(place.iter);
    level.totalQty = (level.totalQty > removedQty) ? (level.totalQty - removedQty) : 0;

    if (level.orders.empty()) {
        book.erase(levelIter);
    }

    orderIndex.erase(idxIt);
    refreshTopOfBook();
}

OrderId OrderBook::generateOrderId() {
    return ++nextOrderId;
}

bool OrderBook::fillOrder(PriceLevel& level, Quantity& remainingQuantity) {
    if (level.orders.empty() || remainingQuantity == 0) {
        return false;
    }

    auto restingIter = level.orders.begin(); //pointer to firt PriceLevel
    Order& restingOrder = *restingIter; // convert to reference
    const Quantity matched = std::min(remainingQuantity, restingOrder.qty);

    remainingQuantity -= matched;
    restingOrder.qty -= matched;

    level.totalQty = (level.totalQty > matched) ? (level.totalQty - matched) : 0;

    if (restingOrder.qty == 0) {
        orderIndex.erase(restingOrder.id);
        level.orders.erase(restingIter);
        return true;
    }
    return false;
}



/// @brief
//////////Helper Functions //////
void OrderBook::refreshTopOfBook(){

    bestBid = bids.empty() ? 0: bids.rbegin() -> first;
    bestAsk = asks.empty() ? 0 : asks.begin() -> first;
}
