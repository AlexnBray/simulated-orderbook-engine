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

## Benchmark

`orderbook_bench 1000000`: 60% limit / 15% market / 25% cancel, prices clustered around a mid.
Measured on WSL2 (Ubuntu 24.04, g++ 13.3, `-O2`). Numbers will vary by machine:

```
ops=1000000  time=0.457s  throughput=2187386 ops/s  trades=701596  resting=31472
op            count    p50(ns)    p99(ns)  p99.9(ns)
limit        600201        296       1085       2833
market       149519        419       1322       3997
cancel       250280         76        660       1021
```

## Thought flow charts

<img width="374" height="143" alt="Image" src="https://github.com/user-attachments/assets/d49c40f9-44cd-43c3-a030-ea4d62c42437" />
