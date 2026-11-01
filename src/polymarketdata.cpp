#include "pch.h"
#include "polymarketdata.h"

#include <cctype>
#include <cstring>
#include <iomanip>

namespace polymarket {

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// "makerAssetId" / "maker_asset_id" / "MakerAssetID" -> "makerassetid"
std::string canonicalColumn(const std::string& s) {
    std::string out;
    for (char c : trim(s))
        if (c != '_' && c != ' ' && c != '"') out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string cur;
    bool inQuotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (inQuotes) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; ++i; }
            else if (c == '"') inQuotes = false;
            else cur += c;
        } else if (c == '"') {
            inQuotes = true;
        } else if (c == ',') {
            fields.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    fields.push_back(trim(cur));
    return fields;
}

// Normalise an address/hash string: lowercase, 0x-prefixed, validated hex of the given byte length.
bool normalizeHexField(const std::string& in, std::size_t bytes, std::string& out) {
    std::vector<std::uint8_t> raw;
    if (!parseHex(in, raw) || raw.size() != bytes) return false;
    out = toHex(raw.data(), raw.size());
    return true;
}

} // namespace

// ---------------- low-level decoding ----------------

bool parseHex(const std::string& hexIn, std::vector<std::uint8_t>& out, std::string* err) {
    std::string hex = trim(hexIn);
    if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex = hex.substr(2);
    if (hex.size() % 2 != 0) {
        if (err) *err = "hex string has odd length";
        return false;
    }
    out.clear();
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = hexNibble(hex[i]), lo = hexNibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            if (err) *err = "invalid hex character";
            return false;
        }
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return true;
}

bool parseWord(const std::string& hex, Word& out, std::string* err) {
    std::vector<std::uint8_t> raw;
    if (!parseHex(hex, raw, err)) return false;
    if (raw.size() != out.size()) {
        if (err) *err = "expected 32-byte word, got " + std::to_string(raw.size()) + " bytes";
        return false;
    }
    std::copy(raw.begin(), raw.end(), out.begin());
    return true;
}

std::string toHex(const std::uint8_t* bytes, std::size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string s = "0x";
    s.reserve(2 + 2 * n);
    for (std::size_t i = 0; i < n; ++i) {
        s += digits[bytes[i] >> 4];
        s += digits[bytes[i] & 0xF];
    }
    return s;
}

std::string wordToDecimal(const Word& w) {
    // Repeated long division of the big-endian 256-bit number by 10.
    Word n = w;
    std::string digits;
    auto isZero = [&] {
        for (auto b : n) if (b) return false;
        return true;
    };
    if (isZero()) return "0";
    while (!isZero()) {
        unsigned rem = 0;
        for (auto& b : n) {
            const unsigned cur = (rem << 8) | b;
            b = static_cast<std::uint8_t>(cur / 10);
            rem = cur % 10;
        }
        digits += static_cast<char>('0' + rem);
    }
    return std::string(digits.rbegin(), digits.rend());
}

std::optional<std::uint64_t> wordToU64(const Word& w) {
    for (std::size_t i = 0; i < 24; ++i)
        if (w[i] != 0) return std::nullopt;
    std::uint64_t v = 0;
    for (std::size_t i = 24; i < 32; ++i) v = (v << 8) | w[i];
    return v;
}

std::string wordToAddress(const Word& w) { return toHex(w.data() + 12, 20); }

bool parseU64(const std::string& decIn, std::uint64_t& out) {
    const std::string dec = trim(decIn);
    if (dec.empty()) return false;
    std::uint64_t v = 0;
    for (char c : dec) {
        if (c < '0' || c > '9') return false;
        const std::uint64_t d = static_cast<std::uint64_t>(c - '0');
        if (v > (std::numeric_limits<std::uint64_t>::max() - d) / 10) return false; // overflow
        v = v * 10 + d;
    }
    out = v;
    return true;
}

std::string normalizeUintString(const std::string& sIn) {
    const std::string s = trim(sIn);
    if (s.empty()) return "";
    for (char c : s)
        if (c < '0' || c > '9') return "";
    const auto nz = s.find_first_not_of('0');
    return nz == std::string::npos ? "0" : s.substr(nz);
}

std::optional<OrderFilledEvent> decodeOrderFilledLog(const std::vector<std::string>& topicHex,
                                                     const std::string& dataHex, std::string* err) {
    if (topicHex.size() != 4) {
        if (err) *err = "OrderFilled log must have 4 topics, got " + std::to_string(topicHex.size());
        return std::nullopt;
    }
    OrderFilledTopics topics{};
    Word* topicWords[4] = {&topics.signature, &topics.orderHash, &topics.maker, &topics.taker};
    for (std::size_t i = 0; i < 4; ++i) {
        std::string e;
        if (!parseWord(topicHex[i], *topicWords[i], &e)) {
            if (err) *err = "topic" + std::to_string(i) + ": " + e;
            return std::nullopt;
        }
    }
    if (toHex(topics.signature.data(), 32) != kOrderFilledTopic0) {
        if (err) *err = "topic0 is not the OrderFilled event signature";
        return std::nullopt;
    }

    std::vector<std::uint8_t> raw;
    std::string e;
    if (!parseHex(dataHex, raw, &e)) {
        if (err) *err = "data: " + e;
        return std::nullopt;
    }
    if (raw.size() != sizeof(OrderFilledData)) {
        if (err) *err = "data must be " + std::to_string(sizeof(OrderFilledData)) + " bytes, got " +
                        std::to_string(raw.size());
        return std::nullopt;
    }
    OrderFilledData data{};
    std::memcpy(&data, raw.data(), sizeof(data)); // packed struct == ABI layout

    OrderFilledEvent ev;
    ev.orderHash = toHex(topics.orderHash.data(), 32);
    ev.maker = wordToAddress(topics.maker);
    ev.taker = wordToAddress(topics.taker);
    ev.makerAssetId = wordToDecimal(data.makerAssetId);
    ev.takerAssetId = wordToDecimal(data.takerAssetId);
    const auto ma = wordToU64(data.makerAmountFilled);
    const auto ta = wordToU64(data.takerAmountFilled);
    const auto fee = wordToU64(data.fee);
    if (!ma || !ta || !fee) {
        if (err) *err = "amount does not fit in 64 bits";
        return std::nullopt;
    }
    ev.makerAmountFilled = *ma;
    ev.takerAmountFilled = *ta;
    ev.fee = *fee;
    return ev;
}

bool isExchangeAddress(const std::string& addr) {
    const std::string a = lower(trim(addr));
    return a == kCtfExchange || a == kNegRiskCtfExchange;
}

std::optional<NormalizedFill> normalize(const OrderFilledEvent& ev, std::string* err) {
    auto bad = [&](const char* why) -> std::optional<NormalizedFill> {
        if (err) *err = why;
        return std::nullopt;
    };
    const bool makerPaysUsdc = ev.makerAssetId == "0";
    const bool takerPaysUsdc = ev.takerAssetId == "0";
    if (makerPaysUsdc == takerPaysUsdc) return bad("exactly one side must be USDC (asset id 0)");
    if (ev.makerAmountFilled == 0 || ev.takerAmountFilled == 0) return bad("zero fill amount");

    NormalizedFill f;
    f.timestamp = ev.timestamp;
    f.maker = ev.maker;
    f.taker = ev.taker;
    if (makerPaysUsdc) {
        // maker gave USDC, received outcome tokens -> maker was bidding
        f.makerSide = Side::Bid;
        f.assetId = ev.takerAssetId;
        f.notional = ev.makerAmountFilled;
        f.qty = ev.takerAmountFilled;
    } else {
        // maker gave outcome tokens, received USDC -> maker was offering
        f.makerSide = Side::Ask;
        f.assetId = ev.makerAssetId;
        f.qty = ev.makerAmountFilled;
        f.notional = ev.takerAmountFilled;
    }
    // price = notional / qty in micro-USDC per share, rounded to nearest
    const UInt128 num = static_cast<UInt128>(f.notional) * kPriceScale + f.qty / 2;
    const UInt128 px = num / f.qty;
    if (px == 0 || px > static_cast<UInt128>(kPriceScale))
        return bad("implied price outside (0, 1] USDC per share");
    f.price = static_cast<Price>(px);
    return f;
}

// ---------------- CSV ingestion ----------------

std::vector<OrderFilledEvent> readOrderFilledCsv(std::istream& in, IngestStats& stats) {
    std::vector<OrderFilledEvent> events;
    std::map<std::string, std::size_t> col;
    bool haveHeader = false, rawMode = false;
    std::string line;
    std::size_t lineNo = 0;

    auto fail = [&](const std::string& why) {
        ++stats.invalidRows;
        stats.errors.push_back("line " + std::to_string(lineNo) + ": " + why);
    };

    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        auto fields = splitCsvLine(t);

        if (!haveHeader) {
            for (std::size_t i = 0; i < fields.size(); ++i) col[canonicalColumn(fields[i])] = i;
            rawMode = col.count("data") && col.count("topic0");
            const std::vector<std::string> required =
                rawMode ? std::vector<std::string>{"topic0", "topic1", "topic2", "topic3", "data"}
                        : std::vector<std::string>{"orderhash", "maker", "taker", "makerassetid", "takerassetid",
                                                   "makeramountfilled", "takeramountfilled"};
            for (const auto& r : required) {
                if (!col.count(r)) {
                    stats.errors.push_back("header is missing required column '" + r + "'");
                    return events;
                }
            }
            haveHeader = true;
            continue;
        }

        ++stats.rowsRead;
        auto get = [&](const std::string& name) -> std::string {
            auto it = col.find(name);
            return (it != col.end() && it->second < fields.size()) ? fields[it->second] : std::string();
        };

        OrderFilledEvent ev;
        if (rawMode) {
            std::string err;
            auto decoded = decodeOrderFilledLog({get("topic0"), get("topic1"), get("topic2"), get("topic3")},
                                                get("data"), &err);
            if (!decoded) { fail(err); continue; }
            ev = *decoded;
        } else {
            if (!normalizeHexField(get("orderhash"), 32, ev.orderHash)) { fail("bad orderHash"); continue; }
            if (!normalizeHexField(get("maker"), 20, ev.maker)) { fail("bad maker address"); continue; }
            if (!normalizeHexField(get("taker"), 20, ev.taker)) { fail("bad taker address"); continue; }
            ev.makerAssetId = normalizeUintString(get("makerassetid"));
            ev.takerAssetId = normalizeUintString(get("takerassetid"));
            if (ev.makerAssetId.empty() || ev.takerAssetId.empty()) { fail("bad asset id"); continue; }
            if (!parseU64(get("makeramountfilled"), ev.makerAmountFilled)) { fail("bad makerAmountFilled"); continue; }
            if (!parseU64(get("takeramountfilled"), ev.takerAmountFilled)) { fail("bad takerAmountFilled"); continue; }
            const std::string fee = get("fee");
            if (!fee.empty() && !parseU64(fee, ev.fee)) { fail("bad fee"); continue; }
        }
        const std::string ts = get("timestamp");
        if (!ts.empty()) {
            std::uint64_t v = 0;
            if (!parseU64(ts, v) || v > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                fail("bad timestamp");
                continue;
            }
            ev.timestamp = static_cast<std::int64_t>(v);
        }
        ev.transactionHash = lower(get("transactionhash"));
        events.push_back(std::move(ev));
        ++stats.eventsParsed;
    }
    if (!haveHeader) stats.errors.push_back("no header row found");
    return events;
}

// ---------------- replay ----------------

const OrderBook* FillReplayer::book(const std::string& assetId) const {
    auto it = books_.find(assetId);
    return it == books_.end() ? nullptr : &it->second;
}

bool FillReplayer::apply(const OrderFilledEvent& ev, IngestStats& stats) {
    if (opts_.skipExchangeLegs && isExchangeAddress(ev.taker)) {
        ++stats.skippedExchangeLegs;
        return false;
    }
    std::string err;
    const auto fill = normalize(ev, &err);
    if (!fill) {
        ++stats.invalidRows;
        stats.errors.push_back("order " + ev.orderHash + ": " + err);
        return false;
    }

    OrderBook& ob = books_[fill->assetId];
    const SubmitResult maker = ob.insertLimit(Order{0, fill->price, fill->qty, fill->makerSide, fill->timestamp});
    const SubmitResult taker =
        ob.insertLimit(Order{0, fill->price, fill->qty, opposite(fill->makerSide), fill->timestamp});

    // The taker must trade exactly against the maker we just placed, at the fill price.
    Quantity tradedWithMaker = 0;
    for (const auto& t : taker.trades)
        if (t.makerId == maker.id && t.price == fill->price) tradedWithMaker += t.qty;
    if (maker.status != OrderStatus::Resting || tradedWithMaker != fill->qty) {
        ++stats.engineMismatches;
        stats.errors.push_back("order " + ev.orderHash + ": engine did not reproduce fill");
    }

    AssetSummary& s = summaries_[fill->assetId];
    s.assetId = fill->assetId;
    auto& pxq = pxQty_[fill->assetId];
    for (const auto& t : taker.trades) {
        if (s.trades == 0) {
            s.open = s.high = s.low = t.price;
            s.firstTimestamp = fill->timestamp;
        }
        ++s.trades;
        s.volume += t.qty;
        s.high = std::max(s.high, t.price);
        s.low = std::min(s.low, t.price);
        s.last = t.price;
        s.lastTimestamp = fill->timestamp;
        pxq += static_cast<UInt128>(t.price) * t.qty;
        if (tradeLog_) {
            *tradeLog_ << fill->timestamp << ',' << fill->assetId << ',' << toString(t.takerSide) << ','
                       << formatPrice(t.price) << ',' << formatShares(t.qty) << ',' << t.makerId << ','
                       << t.takerId << ',' << fill->maker << ',' << fill->taker << '\n';
        }
    }
    if (s.volume > 0) {
        s.vwap = static_cast<Price>((pxq + s.volume / 2) / s.volume);
        s.notional = static_cast<std::uint64_t>((pxq + kPriceScale / 2) / kPriceScale);
    }
    ++stats.fillsReplayed;
    return true;
}

IngestStats ingestCsv(std::istream& in, FillReplayer& replayer) {
    IngestStats stats;
    auto events = readOrderFilledCsv(in, stats);
    std::stable_sort(events.begin(), events.end(),
                     [](const OrderFilledEvent& a, const OrderFilledEvent& b) { return a.timestamp < b.timestamp; });
    for (const auto& ev : events) replayer.apply(ev, stats);
    return stats;
}

// ---------------- formatting ----------------

namespace {
std::string fixed6(std::uint64_t whole, std::uint64_t frac) {
    std::ostringstream ss;
    ss << whole << '.' << std::setw(6) << std::setfill('0') << frac;
    return ss.str();
}
} // namespace

std::string formatPrice(Price p) {
    const bool neg = p < 0;
    const std::uint64_t a = static_cast<std::uint64_t>(neg ? -p : p);
    return (neg ? "-" : "") + fixed6(a / kPriceScale, a % kPriceScale);
}

std::string formatShares(Quantity q) { return fixed6(q / kAmountScale, q % kAmountScale); }

void printSummary(std::ostream& out, const IngestStats& stats, const FillReplayer& replayer) {
    out << "=== POLYMARKET INGEST ===\n"
        << "rows read:              " << stats.rowsRead << '\n'
        << "events parsed:          " << stats.eventsParsed << '\n'
        << "invalid rows:           " << stats.invalidRows << '\n'
        << "skipped exchange legs:  " << stats.skippedExchangeLegs << '\n'
        << "fills replayed:         " << stats.fillsReplayed << '\n'
        << "engine mismatches:      " << stats.engineMismatches << '\n';
    for (const auto& [id, s] : replayer.summaries()) {
        const std::string shortId = id.size() > 16 ? id.substr(0, 8) + "..." + id.substr(id.size() - 6) : id;
        out << "--- asset " << shortId << " ---\n"
            << "  trades " << s.trades << " | volume " << formatShares(s.volume) << " shares | notional "
            << formatShares(s.notional) << " USDC\n"
            << "  open " << formatPrice(s.open) << " high " << formatPrice(s.high) << " low "
            << formatPrice(s.low) << " last " << formatPrice(s.last) << " vwap " << formatPrice(s.vwap) << '\n';
    }
    if (!stats.errors.empty()) {
        out << "--- errors (first 10) ---\n";
        for (std::size_t i = 0; i < stats.errors.size() && i < 10; ++i) out << "  " << stats.errors[i] << '\n';
    }
}

} // namespace polymarket
