# CMake generated Testfile for 
# Source directory: /mnt/c/Users/alexn/Documents/dev/projects/order_book_sim
# Build directory: /mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/build-linux
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(matching "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/build-linux/test_matching")
set_tests_properties(matching PROPERTIES  LABELS "matching;roadmap1" _BACKTRACE_TRIPLES "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;69;add_test;/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;0;")
add_test(invariants "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/build-linux/test_invariants")
set_tests_properties(invariants PROPERTIES  LABELS "invariants;roadmap2" _BACKTRACE_TRIPLES "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;69;add_test;/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;0;")
add_test(edge_cases "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/build-linux/test_edge_cases")
set_tests_properties(edge_cases PROPERTIES  LABELS "edge;roadmap3" _BACKTRACE_TRIPLES "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;69;add_test;/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;0;")
add_test(replay "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/build-linux/test_replay")
set_tests_properties(replay PROPERTIES  LABELS "replay;roadmap7;roadmap3" _BACKTRACE_TRIPLES "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;69;add_test;/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;0;")
add_test(modify "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/build-linux/test_modify")
set_tests_properties(modify PROPERTIES  LABELS "modify;roadmap8" _BACKTRACE_TRIPLES "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;69;add_test;/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;0;")
add_test(snapshots "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/build-linux/test_snapshots")
set_tests_properties(snapshots PROPERTIES  LABELS "snapshots;roadmap9" _BACKTRACE_TRIPLES "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;69;add_test;/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;0;")
add_test(polymarket "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/build-linux/test_polymarket")
set_tests_properties(polymarket PROPERTIES  LABELS "ingest" _BACKTRACE_TRIPLES "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;69;add_test;/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;0;")
add_test(cli_ingest_decoded "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/build-linux/orderbook" "ingest" "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/data/polymarket_sample_fills.csv")
set_tests_properties(cli_ingest_decoded PROPERTIES  LABELS "ingest;cli" PASS_REGULAR_EXPRESSION "fills replayed: +6.*engine mismatches: +0" _BACKTRACE_TRIPLES "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;74;add_test;/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;0;")
add_test(cli_ingest_rawlogs "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/build-linux/orderbook" "ingest" "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/data/polymarket_sample_rawlogs.csv")
set_tests_properties(cli_ingest_rawlogs PROPERTIES  LABELS "ingest;cli" PASS_REGULAR_EXPRESSION "fills replayed: +6.*engine mismatches: +0" _BACKTRACE_TRIPLES "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;76;add_test;/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;0;")
add_test(cli_replay_demo "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/build-linux/orderbook" "replay" "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/scenarios/demo_session.txt")
set_tests_properties(cli_replay_demo PROPERTIES  LABELS "replay;cli" PASS_REGULAR_EXPRESSION "BestBid: 100 \\| BestAsk: 102.*commands=11 rejected=0 trades=4" _BACKTRACE_TRIPLES "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;82;add_test;/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;0;")
add_test(bench_smoke "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/build-linux/orderbook_bench" "20000")
set_tests_properties(bench_smoke PROPERTIES  LABELS "bench" _BACKTRACE_TRIPLES "/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;89;add_test;/mnt/c/Users/alexn/Documents/dev/projects/order_book_sim/CMakeLists.txt;0;")
