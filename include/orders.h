#pragma once
#include <string>
#include <ctime>


enum side {
    BUY,
    SELL
}

struct Order {
    int id;
    double price;
    float qty;
    side side;
    std::time_t timestamp;
};



/*
BUY / SELL
TYPE
PRICE
QUANTITY


TIMESTAMP

therefore, a change is allowing the user to choose which type of order they wish to place potentially. 



*/