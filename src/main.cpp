#include "pch.h"

int main(){
    std::cout << "test output.. [1.06]";
    std::vector<int> vec = {1, 2, 3, 4, 5};
    for (int& i : vec) {
        std::cout << i << '\n';
    }
    std::cout << std::endl;
    
    return 0;
}