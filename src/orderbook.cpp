#include "pch.h"

void OrderBook::insertLimit(Order order) {
    /*
    Picks what side to place the order on, then finds/creates the price level in that map
    */
    // Duplicate ID policy on insert

    auto idxIt = orderIndex.find(order.id);
    if (idxIt != orderIndex.end()) {
        std::cout << "This order already exists" << '\n';
        return;
    }
    auto& book = (order.side == Side::Bid) ? bids : asks; // reference to either Side::Bid or Side::Ask
    Quantity remaining = order.qty;
    if (order.side == Side::Bid && !asks.empty() && order.price >= asks.begin()->first) {
        matchAgainstBook(order.side, remaining, false, order.price); 
        if (remaining == 0) return;
        order.qty = remaining;
    }
    if (order.side == Side::Ask && !bids.empty() && order.price <= bids.rbegin()-> first) {
        matchAgainstBook(order.side, remaining, false, order.price); 
        if (remaining == 0) return;
        order.qty = remaining;
    }
    auto& level = book[order.price]; // if key exists returns exisitng price level reference, else creates a PriceLevel object then returns that.

    level.price = order.price;
    level.orders.push_back(order);
    level.totalQty += order.qty;

    auto iter = level.orders.end(); //end points to the element after the last element
    --iter; // iter should now point to the last element

    orderIndex[order.id] = OrderLocation{order.price, order.side, iter}; //unordered_map

    refreshTopOfBook();
}

/*
Fix the return nothing ### Done
Duplicate ID policy on insert ####Done
Tighter underflow assertions for qty invariants
tests for cancel edge-cases

*/

void OrderBook::insertMarket(Side side, Quantity qty) {
    matchAgainstBook(side, qty, true);
}

Quantity OrderBook::matchAgainstBook(Side side, Quantity& remainingQty, bool isMarket, Price limitPrice){

    auto initialremaining = remainingQty;

    auto& book = (side == Side::Bid) ? asks : bids;
    if (isMarket){
        while (remainingQty > 0 && !book.empty()) {
            auto levelIter = (side == Side::Bid) ? book.begin() : std::prev(book.end());
            auto& level = levelIter->second;

            while (remainingQty > 0 && !level.orders.empty()) {
                fillOrder(level, remainingQty);
            }

            if (level.orders.empty()) {
                levelIter= book.erase(levelIter);
            }
        } 
    } else {
         while (remainingQty > 0 && !book.empty()) {
            auto levelIter = (side == Side::Bid) ? book.begin() : std::prev(book.end());
            auto& level = levelIter->second;

            if (side == Side::Bid && levelIter ->first > limitPrice) break;
            if (side == Side::Ask && levelIter ->first < limitPrice) break;
    
            while (remainingQty > 0 && !level.orders.empty()) {
                fillOrder(level, remainingQty);
            }

            if (level.orders.empty()) {
                levelIter = book.erase(levelIter);
            }
         }
    }
    auto filledQty {initialremaining -remainingQty};
    refreshTopOfBook();
    return filledQty;

}


void OrderBook::cancel(OrderId id) {

    // index iterator (points to it, must be converted)
    auto idxIt = orderIndex.find(id); // find gives an iterator to a map element and map elements are pairs
    if (idxIt == orderIndex.end()) return;

    // Copy location so we can safely erase index entry early.
    const OrderLocation loc = idxIt->second;
    auto& book = (loc.side == Side::Bid) ? bids : asks; // what side it is

    auto levelIter = book.find(loc.price); // again gives an iterator
    if (levelIter == book.end()) return; // checks if it exists in the map, if not continue

    auto& level = levelIter->second; // maps it to the price level (second entry)
    if (loc.iter == level.orders.end()) return;

    const Quantity removedQty = loc.iter->qty; // iterator pointing to one order node
    // this points to the node, then accesses the order's quantity field
    orderIndex.erase(idxIt); // prevent stale iterator in index after erase
    level.orders.erase(loc.iter);
    level.totalQty = (level.totalQty > removedQty) ? (level.totalQty - removedQty) : 0;

    if (level.orders.empty()) {
        book.erase(levelIter);
    }

    refreshTopOfBook();
}

OrderId OrderBook::generateOrderId() {
    return ++nextOrderId;
}

bool OrderBook::fillOrder(PriceLevel& level, Quantity& remainingQuantity) {
    if (level.orders.empty() || remainingQuantity == 0) {
        return false;
    }

    auto restingIter = level.orders.begin(); //pointer to first PriceLevel
    Order& restingOrder = *restingIter; // convert to reference
    const Quantity matched = std::min(remainingQuantity, restingOrder.qty);

    remainingQuantity -= matched;
    restingOrder.qty -= matched;

    level.totalQty = (level.totalQty > matched) ? (level.totalQty - matched) : 0;

    if (restingOrder.qty == 0) {
        orderIndex.erase(restingOrder.id);
        restingIter = level.orders.erase(restingIter);
        return true;
    }
    return false;
}



void OrderBook::printBook() const {
    std::cout << "=== ORDER BOOK ===\n";
    std::cout << "BestBid: " << bestBid << " | BestAsk: " << bestAsk << " | Spread: " << getSpread() <<'\n';

    std::cout << "--- Asks (low -> high) ---\n";
    if (asks.empty()) {
        std::cout << "(empty)\n";
    } else {
        for (const auto& [price, level] : asks) {
            std::cout << "Price " << price << " | LevelQty " << level.totalQty << '\n';
            for (const auto& order : level.orders) {
                std::cout << "  id=" << order.id
                          << " qty=" << order.qty
                          << " side=Ask"
                          << " ts=" << order.timestamp << '\n';
            }
        }
    }

    std::cout << "--- Bids (high -> low) ---\n";
    if (bids.empty()) {
        std::cout << "(empty)\n";
    } else {
        for (auto it = bids.rbegin(); it != bids.rend(); ++it) {
            const auto& price = it->first;
            const auto& level = it->second;
            std::cout << "Price " << price << " | LevelQty " << level.totalQty << '\n';
            for (const auto& order : level.orders) {
                std::cout << "  id=" << order.id
                          << " qty=" << order.qty
                          << " side=Bid"
                          << " ts=" << order.timestamp << '\n';
            }
        }
    }

    std::cout << "==================\n";
}



/// @brief
//////////Helper Functions //////
void OrderBook::refreshTopOfBook(){

    bestBid = bids.empty() ? 0: bids.rbegin() -> first;
    bestAsk = asks.empty() ? 0 : asks.begin() -> first;
}

