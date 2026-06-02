cmake_minimum_required(VERSION 3.16)
project(order_book_sim LANGUAGES CXX)

add_executable(orderbook
    src/main.cpp
    src/orders.cpp
)

target_compile_features(orderbook PRIVATE cxx_std_17)

target_include_directories(orderbook
    PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
)

# Enable PCH when present to match source includes.
if(EXISTS "${PROJECT_SOURCE_DIR}/include/pch.h")
    target_precompile_headers(orderbook
        PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:${PROJECT_SOURCE_DIR}/include/pch.h>"
    )
endif()