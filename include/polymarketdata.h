#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <vector>
#include "orderbook.h"

/*
Polymarket CTF Exchange ingestion.

Source event (Polygon), emitted once per order filled in a match:

    event OrderFilled(bytes32 indexed orderHash, address indexed maker, address indexed taker,
                      uint256 makerAssetId, uint256 takerAssetId,
                      uint256 makerAmountFilled, uint256 takerAmountFilled, uint256 fee);

Asset id 0 is the USDC collateral; any other id is an outcome (ERC-1155) token.
USDC and outcome tokens both use 6 decimals.

Pipeline:  CSV/raw log -> OrderFilledEvent -> NormalizedFill -> FillReplayer (OrderBook per asset)
           -> engine trades -> per-asset summary (volume, VWAP, OHLC) + optional trade log.
*/
namespace polymarket {

using Word = std::array<std::uint8_t, 32>; // one 32-byte ABI word / log topic

// 128-bit intermediate for price*qty products (GCC/Clang extension; MinGW and Linux toolchains support it)
__extension__ typedef unsigned __int128 UInt128;

// keccak256("OrderFilled(bytes32,address,address,uint256,uint256,uint256,uint256,uint256)")
inline constexpr const char* kOrderFilledTopic0 =
    "0xd0a08e8c493f9c94f29311604c9de1b4e8c8d4c06bd0c789af57f2d65bfec0f6";

// Engine price units: 1 USDC per share == kPriceScale  (i.e. micro-USDC per share)
inline constexpr Price kPriceScale = 1'000'000;
// On-chain base units per whole share / whole USDC (6 decimals)
inline constexpr std::uint64_t kAmountScale = 1'000'000;

// When a taker order is matched against makers, the exchange also emits an
// OrderFilled for the taker's own order with taker == the exchange contract.
// That leg duplicates volume already reported by the maker legs.
inline constexpr const char* kCtfExchange        = "0x4bfb41d5b3570defd03c39a9a4d8de6bd8b8982e";
inline constexpr const char* kNegRiskCtfExchange = "0xc5d563a36ae78145c45a50134d48a1215220f80a";

#pragma pack(push, 1)
// Raw log layout, byte-for-byte (no padding): topics[0..3] and the ABI-encoded data blob.
struct OrderFilledTopics {
    Word signature; // topic0
    Word orderHash; // topic1
    Word maker;     // topic2 (address, left-padded to 32 bytes)
    Word taker;     // topic3
};
struct OrderFilledData {
    Word makerAssetId;
    Word takerAssetId;
    Word makerAmountFilled;
    Word takerAmountFilled;
    Word fee;
};
#pragma pack(pop)
static_assert(sizeof(OrderFilledTopics) == 4 * 32, "topics must be tightly packed");
static_assert(sizeof(OrderFilledData) == 5 * 32, "data must be tightly packed");

struct OrderFilledEvent {
    std::int64_t timestamp{0};
    std::string transactionHash; // optional
    std::string orderHash;       // lowercase 0x-hex
    std::string maker;           // lowercase 0x-hex, 20 bytes
    std::string taker;
    std::string makerAssetId;    // uint256 as decimal string, "0" = USDC
    std::string takerAssetId;
    std::uint64_t makerAmountFilled{0}; // base units (1e-6)
    std::uint64_t takerAmountFilled{0};
    std::uint64_t fee{0};
};

struct NormalizedFill {
    std::string assetId;       // outcome token traded
    Side makerSide{Side::Bid}; // Bid: maker paid USDC for tokens. Ask: maker sold tokens for USDC.
    Price price{0};            // USDC per share * kPriceScale (rounded to nearest)
    Quantity qty{0};           // shares in base units
    std::uint64_t notional{0}; // USDC base units exchanged
    std::int64_t timestamp{0};
    std::string maker;
    std::string taker;
};

// ---- low-level decoding ----
bool parseHex(const std::string& hex, std::vector<std::uint8_t>& out, std::string* err = nullptr);
bool parseWord(const std::string& hex, Word& out, std::string* err = nullptr);
std::string toHex(const std::uint8_t* bytes, std::size_t n); // "0x..." lowercase
std::string wordToDecimal(const Word& w);                       // full uint256 -> decimal
std::optional<std::uint64_t> wordToU64(const Word& w);          // nullopt if it overflows 64 bits
std::string wordToAddress(const Word& w);                       // low 20 bytes as 0x-hex
bool parseU64(const std::string& dec, std::uint64_t& out);
std::string normalizeUintString(const std::string& s);         // strips leading zeros, validates digits ("" if invalid)

// Decode a raw eth_getLogs entry. topics must have 4 entries; dataHex is 5*32 bytes.
std::optional<OrderFilledEvent> decodeOrderFilledLog(const std::vector<std::string>& topics,
                                                     const std::string& dataHex, std::string* err = nullptr);

// Convert an event into engine units. Fails if neither side is USDC, both are,
// amounts are zero, or the implied price is outside (0, 1] USDC.
std::optional<NormalizedFill> normalize(const OrderFilledEvent& ev, std::string* err = nullptr);

bool isExchangeAddress(const std::string& addr);

// ---- CSV ingestion ----
struct IngestStats {
    std::size_t rowsRead{0};
    std::size_t eventsParsed{0};
    std::size_t invalidRows{0};
    std::size_t skippedExchangeLegs{0};
    std::size_t fillsReplayed{0};
    std::size_t engineMismatches{0}; // engine trade did not reproduce the on-chain fill
    std::vector<std::string> errors;
};

/*
Header-driven CSV reader (comma separated, optional double quotes, '#' comment lines).
Two layouts are accepted, detected from the header:
  1) Decoded events, e.g. a Goldsky/Polymarket subgraph `orderFilledEvents` export:
       timestamp, orderHash, maker, taker, makerAssetId, takerAssetId,
       makerAmountFilled, takerAmountFilled, fee [, transactionHash]
     (camelCase or snake_case column names, any order, extra columns ignored)
  2) Raw logs: topic0, topic1, topic2, topic3, data [, timestamp] [, transactionHash]
*/
std::vector<OrderFilledEvent> readOrderFilledCsv(std::istream& in, IngestStats& stats);

struct AssetSummary {
    std::string assetId;
    std::size_t trades{0};
    Quantity volume{0};          // share base units
    std::uint64_t notional{0};   // sum(price*qty)/kPriceScale, USDC base units (engine-derived)
    Price vwap{0}, open{0}, high{0}, low{0}, last{0};
    std::int64_t firstTimestamp{0}, lastTimestamp{0};
};

// Replays fills through one OrderBook per outcome token: the maker's order is
// placed as a resting limit at the fill price, then the taker's order arrives
// on the opposite side at the same price and crosses it. Summaries are built
// from the trades the engine actually produced.
class FillReplayer {
public:
    struct Options { bool skipExchangeLegs = true; };

    FillReplayer() = default;
    explicit FillReplayer(Options opts) : opts_(opts) {}

    void setTradeLog(std::ostream* out) { tradeLog_ = out; }
    bool apply(const OrderFilledEvent& ev, IngestStats& stats); // false if skipped/invalid

    const std::map<std::string, AssetSummary>& summaries() const { return summaries_; }
    const OrderBook* book(const std::string& assetId) const;

private:
    Options opts_{};
    std::ostream* tradeLog_{nullptr};
    std::map<std::string, OrderBook> books_;
    std::map<std::string, AssetSummary> summaries_;
    std::map<std::string, UInt128> pxQty_; // running sum(price*qty) for VWAP
};

// Convenience: read CSV, sort by timestamp (stable), replay everything.
IngestStats ingestCsv(std::istream& in, FillReplayer& replayer);

std::string formatPrice(Price p);       // engine price -> "0.523000"
std::string formatShares(Quantity q);   // base units -> "12.500000"
void printSummary(std::ostream& out, const IngestStats& stats, const FillReplayer& replayer);

} // namespace polymarket
