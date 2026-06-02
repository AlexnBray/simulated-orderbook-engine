#pragma once
#include <string>
#include <ctime>

struct Order {
    int id;
    double price;
    float qty;
    std::string side;
    std::time_t timestamp;

    Order(int id, double price, float qty, const std::string& side, std::time_t timestamp);
};
