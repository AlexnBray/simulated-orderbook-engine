[![Build](https://github.com/AlexnBray/simulated-orderbook-engine/actions/workflows/build.yml/badge.svg)](https://github.com/AlexnBray/simulated-orderbook-engine/actions/workflows/build.yml)

# Simulated Order Book Engine

A single-threaded, deterministic limit order book in C++17 with price-time priority,
market/limit/cancel/modify flows, L1/L2/L3 snapshots, an append-only event log with
exact replay, and an ingestion pipeline that replays **Polymarket CTF Exchange
`OrderFilled` events** through the engine.

## Build & test

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

No third-party dependencies: the tests use a ~100-line in-repo harness (`tests/test_framework.h`).
Sanitizer build: add `-DOBS_SANITIZE=ON` (ASan + UBSan; GCC/Clang).

> Toolchain note: GCC or Clang (Linux, or MinGW/MSYS2 on Windows). The Polymarket price
> math uses `unsigned __int128`, which MSVC does not provide.

## Verify a topic

Every roadmap topic has its own test executable and ctest label, so each one can be checked on its own:

| Topic | Command | What it proves |
|---|---|---|
| 1. Matching semantics | `ctest --test-dir build -L matching` | price priority, FIFO within a level, market sweeps, crossing limit + resting remainder, partial/full fills, determinism with identical timestamps |
| 2. Invariant-safe state | `ctest --test-dir build -L invariants` | index/level/aggregate consistency, no stale iterators, empty-level cleanup, best bid/ask on every path, duplicate-ID policy, **20k-op randomized differential test against a naive reference engine** |
| 3. Edge cases + regressions | `ctest --test-dir build -L roadmap3` | empty book, unknown IDs, zero qty, bad prices, multi-level sweeps, >32-bit quantities, scenario regressions |
| 7. Event log + replay | `ctest --test-dir build -L replay` | log format, malformed-line handling, replaying a 3,000-command log reproduces the identical book and trades |
| 8. Modify / replace | `ctest --test-dir build -L modify` | reduce keeps priority, increase/price change loses it, crossing on modify, modify-to-zero |
| 9. Snapshots | `ctest --test-dir build -L snapshots` | L1 top of book, L2 aggregated depth, L3 per-order snapshot |
| Data ingestion | `ctest --test-dir build -L ingest` | hex/uint256/ABI decoding, raw-log and CSV parsing, normalization, replay, VWAP/OHLC, both sample files agree |
| Benchmark | `ctest --test-dir build -L bench` | smoke run of the benchmark plus a post-run invariant check |

Run one binary directly for per-case output, optionally with a name filter:
`./build/test_matching` or `./build/test_matching priority`.

## CLI

```bash
./build/orderbook                                              # interactive: limit / market / cancel / quit
./build/orderbook replay scenarios/demo_session.txt --log events.log
./build/orderbook replay events.log                            # identical final book
./build/orderbook ingest data/polymarket_sample_fills.csv --trades trades.csv
./build/orderbook ingest data/polymarket_sample_rawlogs.csv
./build/orderbook_bench 1000000
```

Scenario / event-log format (one command per line, `#` comments):

```
LIMIT  <BID|ASK> <price> <qty> [id]
MARKET <BID|ASK> <qty>
CANCEL <id>
MODIFY <id> <price> <qty>
FILL   <price> <qty> <makerId> <takerId>    # engine output, ignored on replay
```

## Polymarket data ingestion

Source event (Polygon, CTF Exchange):

```solidity
event OrderFilled(bytes32 indexed orderHash, address indexed maker, address indexed taker,
                  uint256 makerAssetId, uint256 takerAssetId,
                  uint256 makerAmountFilled, uint256 takerAmountFilled, uint256 fee);
```

Pipeline: `CSV / raw log → OrderFilledEvent → NormalizedFill → FillReplayer (one OrderBook per outcome token) → trades → summary`

* **Inputs** (auto-detected from the CSV header):
  * decoded events, e.g. a Polymarket orderbook-subgraph `orderFilledEvents` export
    (`timestamp, orderHash, maker, taker, makerAssetId, takerAssetId, makerAmountFilled, takerAmountFilled, fee`;
    camelCase or snake_case);
  * raw `eth_getLogs` entries (`topic0..topic3, data`). These are decoded through `#pragma pack(1)`
    structs that mirror the ABI layout byte for byte (4 topics, 5 × 32-byte words). topic0 is checked
    against `keccak256("OrderFilled(bytes32,address,address,uint256,uint256,uint256,uint256,uint256)")`.
* **Normalization:** asset id `0` is USDC. If the maker paid USDC, the maker was a **bid**;
  if the maker paid outcome tokens, the maker was an **ask**. Price = USDC / shares in
  micro-USDC (rounded to nearest), and quantities stay in 1e-6 share base units, so there is no
  floating point anywhere. Events with no USDC leg, zero amounts, or a price above 1 USDC are rejected.
* **Duplicate legs:** the exchange also emits an `OrderFilled` for the taker's own order with
  `taker = exchange contract`. Those rows are skipped by default (`FillReplayer::Options::skipExchangeLegs`)
  so volume isn't double-counted.
* **Replay:** each fill places the maker's order as a resting limit order, then sends the taker's
  opposite order at the same price. The summary (volume, notional, VWAP, OHLC) is built from the trades
  the **engine** produced, and `engine mismatches` counts any fill the engine failed to reproduce.

`data/` holds a small **synthetic** sample in both formats. Point `ingest` at a real export to use real data.

## Design

| Structure | Purpose |
|---|---|
| `std::map<Price, PriceLevel>` per side | ordered levels; best bid = `rbegin`, best ask = `begin` |
| `std::list<Order>` per level | FIFO queue; stable iterators allow O(1) erase from the middle |
| `PoolAllocator` for all node containers | per-order allocation is a free-list pop instead of `malloc` |
| `std::unordered_map<OrderId, OrderLocation>` | O(1) lookup of an order's level and list node for cancel/modify |

| Operation | Complexity (L = price levels, k = orders filled) |
|---|---|
| insert resting limit | O(log L) |
| match | O(k + levels emptied · log L) |
| cancel | O(log L) level lookup + O(1) unlink |
| modify (reduce) | O(1) |
| best bid/ask | O(1) (cached, refreshed through one function) |

Policies: id `0` means the engine assigns an id, and engine ids always stay above any caller-supplied id.
Duplicate live ids are rejected. Trades print at the maker's price. Market remainders are dropped, and a
market order into an empty book is rejected. `checkInvariants()` validates the whole structure and runs
after every operation in the tests.

## Benchmarks

**Setup:** Intel Core Ultra 7 258V, WSL2 Ubuntu 24.04, g++ 13.3 `-O3 -DNDEBUG`, single thread pinned
with `taskset -c 2`, fixed seed 42. Workload: 1M operations, 60% limit (about half cross) / 15% market /
25% cancel. Before/after binaries were run **interleaved** (A, B, A, B…) and the tables show the
**median** across rounds. Latency includes about 20–30 ns of `steady_clock` overhead per sample.
Reproduce **every table below** (rebuilds the baseline from commit `45fed4c`, step 1 and the current engine,
then runs all three benchmarks, about 2 minutes): `bash bench/reproduce.sh [out_dir] [cpu]`.

### Optimization story (7 interleaved rounds, empty starting book)

| Version | Throughput | Heap allocs/op | Limit p50 / p99 / p99.9 | Market p50 / p99 / p99.9 | Cancel p50 / p99 / p99.9 |
|---|---|---|---|---|---|
| Baseline | 2.36 M ops/s | 3.08 | 295 / 1150 / 3051 ns | 420 / 1412 / 5272 ns | 80 / 676 / 1143 ns |
| + no log formatting when logging is off | 4.90 M ops/s | 1.78 | 148 / 491 / 1620 ns | 155 / 534 / 1411 ns | 71 / 428 / 667 ns |
| + pooled node allocator | **5.76 M ops/s** | **0.68** | **125 / 343 / 581 ns** | **134 / 399 / 707 ns** | **67 / 312 / 524 ns** |

1. **Logging on the hot path:** every command built `std::to_string` strings for the event log even when no
   log was attached. Guarding them removed 1.3 allocations per operation.
2. **Pool allocator** (`include/pool_allocator.h`): list nodes, hash-index nodes and price-level nodes come from
   a per-thread free list of fixed-size slots instead of `malloc`. Insert p99 went from 1150 to 343 ns
   (−70%), and p99.9 from 3051 to 581 ns (−81%).

Net: **2.4× throughput and −70% to −87% tail latency**, with identical output (same 701,596 trades,
all tests passing under ASan/UBSan). The slowest optimized run (4.41 M ops/s) beat the fastest baseline
run (2.75 M ops/s).

### Scaling with book depth (5 interleaved rounds)

`prefill` rests extra orders outside the active price band before timing. Median latencies are in ns.

| Resting orders | Version | Throughput | Limit p50 / p99 | Market p50 / p99 | Cancel p50 / p99 |
|---|---|---|---|---|---|
| 0 | before → after | 2.14 → 5.12 M/s | 343/1272 → 135/401 | 471/1577 → 136/465 | 96/659 → 76/416 |
| 10,000 | before → after | 2.41 → 5.26 M/s | 296/1072 → 131/359 | 414/1295 → 132/405 | 92/754 → 71/515 |
| 100,000 | before → after | 2.28 → 4.45 M/s | 285/1062 → 123/403 | 419/1401 → 128/579 | 185/1313 → 165/896 |
| 1,000,000 | before → after | 1.89 → 3.27 M/s | 265/1120 → 126/387 | 439/1660 → 132/791 | 682/1407 → 591/1130 |

Insert and market latency stay **flat from an empty book to 1M resting orders**, since they only touch the
top of the book. Cancel latency grows with book size (about 70 → 590 ns p50): a random cancel in a large
book is a cache miss on the hash index and on the list node. That is the next optimization target,
for example a flat order array indexed by id instead of node-based containers.

### Ingestion throughput (1M synthetic `OrderFilled` events, 50 outcome tokens, 25% exchange legs)

Generated with `bench/gen_synthetic_fills.py`. End-to-end wall time (read → parse → sort → normalize →
match → summarize), median of 3 runs:

| Input | File size | Wall time | Throughput | Result |
|---|---|---|---|---|
| Decoded CSV | 263 MB | 5.06 s | **≈198k events/s** | 749,683 fills replayed, 0 mismatches |
| Raw logs (hex topics + ABI data) | 602 MB | 13.4 s | **≈75k events/s** | identical summary |

Peak memory is about 630 MB, because all events are loaded before sorting by timestamp. Streaming
pre-sorted input would remove that. Parsing, not matching, dominates here.

> On native Windows (MinGW), `steady_clock` ticks every 100 ns, so latencies appear rounded to 100 ns.
> Use Linux for latency numbers.

## Thought flow charts

<img width="374" height="143" alt="Image" src="https://github.com/user-attachments/assets/d49c40f9-44cd-43c3-a030-ea4d62c42437" />
