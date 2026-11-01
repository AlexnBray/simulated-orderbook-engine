cmake_minimum_required(VERSION 3.16)
project(order_book_sim LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(OBS_BUILD_TESTS "Build the test suite" ON)
option(OBS_BUILD_BENCH "Build the benchmark" ON)
option(OBS_SANITIZE    "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(OBS_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

function(obs_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ---- engine + ingestion library ----
add_library(orderbook_core STATIC
    src/Order.cpp
    src/orderbook.cpp
    src/scenario.cpp
    src/polymarketdata.cpp
)
target_include_directories(orderbook_core PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_compile_features(orderbook_core PUBLIC cxx_std_17)
target_precompile_headers(orderbook_core PRIVATE "${PROJECT_SOURCE_DIR}/include/pch.h")
obs_warnings(orderbook_core)

# ---- CLI ----
add_executable(orderbook src/main.cpp)
target_link_libraries(orderbook PRIVATE orderbook_core)
obs_warnings(orderbook)

# ---- benchmark ----
if(OBS_BUILD_BENCH)
    add_executable(orderbook_bench bench/benchmark.cpp)
    target_link_libraries(orderbook_bench PRIVATE orderbook_core)
    obs_warnings(orderbook_bench)
endif()

# ---- tests: one executable per roadmap topic, selectable with `ctest -L <label>` ----
if(OBS_BUILD_TESTS)
    enable_testing()

    # topic      -> labels
    set(OBS_TEST_matching    "matching;roadmap1")
    set(OBS_TEST_invariants  "invariants;roadmap2")
    set(OBS_TEST_edge_cases  "edge;roadmap3")
    set(OBS_TEST_replay      "replay;roadmap7;roadmap3")
    set(OBS_TEST_modify      "modify;roadmap8")
    set(OBS_TEST_snapshots   "snapshots;roadmap9")
    set(OBS_TEST_polymarket  "ingest")

    foreach(topic matching invariants edge_cases replay modify snapshots polymarket)
        add_executable(test_${topic} tests/test_${topic}.cpp)
        target_link_libraries(test_${topic} PRIVATE orderbook_core)
        target_compile_definitions(test_${topic} PRIVATE OBS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
        obs_warnings(test_${topic})
        add_test(NAME ${topic} COMMAND test_${topic})
        set_tests_properties(${topic} PROPERTIES LABELS "${OBS_TEST_${topic}}")
    endforeach()

    # end-to-end CLI checks
    add_test(NAME cli_ingest_decoded
             COMMAND orderbook ingest "${PROJECT_SOURCE_DIR}/data/polymarket_sample_fills.csv")
    add_test(NAME cli_ingest_rawlogs
             COMMAND orderbook ingest "${PROJECT_SOURCE_DIR}/data/polymarket_sample_rawlogs.csv")
    set_tests_properties(cli_ingest_decoded cli_ingest_rawlogs PROPERTIES
        LABELS "ingest;cli"
        PASS_REGULAR_EXPRESSION "fills replayed: +6.*engine mismatches: +0")

    add_test(NAME cli_replay_demo
             COMMAND orderbook replay "${PROJECT_SOURCE_DIR}/scenarios/demo_session.txt")
    set_tests_properties(cli_replay_demo PROPERTIES
        LABELS "replay;cli"
        PASS_REGULAR_EXPRESSION "BestBid: 100 \\| BestAsk: 102.*commands=11 rejected=0 trades=4")

    if(OBS_BUILD_BENCH)
        add_test(NAME bench_smoke COMMAND orderbook_bench 20000)
        set_tests_properties(bench_smoke PROPERTIES LABELS "bench")
    endif()
endif()
