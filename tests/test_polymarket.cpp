// Data ingestion: Polymarket CTF Exchange OrderFilled events -> engine replay
#include <fstream>
#include <sstream>
#include "polymarketdata.h"
#include "test_helpers.h"

#ifndef OBS_SOURCE_DIR
#define OBS_SOURCE_DIR "."
#endif

using namespace polymarket;

namespace {
const std::string kYes = "15260181590830166131860913909960308246281948219935181909378657975432319487574";
const std::string kMaker = "0xa1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1";
const std::string kTaker = "0xb1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1";
const std::string kHash = "0x" + std::string(64, '1');

std::string word(std::uint64_t v) {
    std::ostringstream ss;
    ss << std::hex;
    ss.width(64);
    ss.fill('0');
    ss << v;
    return ss.str();
}
std::string padAddr(const std::string& a) { return "0x" + std::string(24, '0') + a.substr(2); }

OrderFilledEvent event(const std::string& makerAsset, const std::string& takerAsset, std::uint64_t makerAmt,
                       std::uint64_t takerAmt, std::int64_t ts = 1, const std::string& taker = kTaker) {
    OrderFilledEvent e;
    e.timestamp = ts;
    e.orderHash = kHash;
    e.maker = kMaker;
    e.taker = taker;
    e.makerAssetId = makerAsset;
    e.takerAssetId = takerAsset;
    e.makerAmountFilled = makerAmt;
    e.takerAmountFilled = takerAmt;
    return e;
}
} // namespace

// ---------- decoding primitives ----------

TEST_CASE(packed_structs_match_abi_layout) {
    CHECK_EQ(sizeof(OrderFilledTopics), 128u);
    CHECK_EQ(sizeof(OrderFilledData), 160u);
}

TEST_CASE(parse_hex_handles_prefix_case_and_errors) {
    std::vector<std::uint8_t> out;
    REQUIRE(parseHex("0xDEadBEef", out));
    CHECK_EQ(out.size(), 4u);
    CHECK_EQ(static_cast<int>(out[0]), 0xde);
    CHECK_EQ(static_cast<int>(out[3]), 0xef);
    CHECK(parseHex("", out) && out.empty());
    std::string err;
    CHECK(!parseHex("0xabc", out, &err));
    CHECK(!err.empty());
    CHECK(!parseHex("0xzz", out));
}

TEST_CASE(word_to_decimal_full_uint256) {
    Word w{};
    CHECK_EQ(wordToDecimal(w), std::string("0"));
    w[31] = 0x0a;
    CHECK_EQ(wordToDecimal(w), std::string("10"));
    w.fill(0xff);
    CHECK_EQ(wordToDecimal(w),
             std::string("115792089237316195423570985008687907853269984665640564039457584007913129639935"));
}

TEST_CASE(word_to_u64_detects_overflow) {
    Word w{};
    w[31] = 1;
    w[24] = 0x80;
    REQUIRE(wordToU64(w).has_value());
    CHECK_EQ(*wordToU64(w), 0x8000000000000001ULL);
    w[23] = 1;
    CHECK(!wordToU64(w).has_value());
}

TEST_CASE(word_to_address_takes_low_20_bytes) {
    Word w{};
    REQUIRE(parseWord(padAddr(kMaker), w));
    CHECK_EQ(wordToAddress(w), kMaker);
}

TEST_CASE(parse_u64_and_uint_strings) {
    std::uint64_t v = 0;
    CHECK(parseU64("18446744073709551615", v));
    CHECK_EQ(v, 18446744073709551615ULL);
    CHECK(!parseU64("18446744073709551616", v)); // overflow
    CHECK(!parseU64("12a", v));
    CHECK(!parseU64("", v));
    CHECK_EQ(normalizeUintString("000123"), std::string("123"));
    CHECK_EQ(normalizeUintString("000"), std::string("0"));
    CHECK_EQ(normalizeUintString("-1"), std::string(""));
}

// ---------- raw log decoding ----------

TEST_CASE(decode_raw_order_filled_log) {
    const std::string data = "0x" + word(0) + std::string(64, 'f') /* takerAssetId = 2^256-1 */ + word(5'200'000) +
                             word(10'000'000) + word(7);
    std::string err;
    const auto ev = decodeOrderFilledLog({kOrderFilledTopic0, kHash, padAddr(kMaker), padAddr(kTaker)}, data, &err);
    REQUIRE(ev.has_value());
    CHECK_EQ(ev->orderHash, kHash);
    CHECK_EQ(ev->maker, kMaker);
    CHECK_EQ(ev->taker, kTaker);
    CHECK_EQ(ev->makerAssetId, std::string("0"));
    CHECK_EQ(ev->takerAssetId,
             std::string("115792089237316195423570985008687907853269984665640564039457584007913129639935"));
    CHECK_EQ(ev->makerAmountFilled, 5'200'000u);
    CHECK_EQ(ev->takerAmountFilled, 10'000'000u);
    CHECK_EQ(ev->fee, 7u);
}

TEST_CASE(decode_rejects_wrong_signature_length_and_overflow) {
    const std::string good = "0x" + word(0) + word(1) + word(2) + word(3) + word(4);
    std::string err;
    CHECK(!decodeOrderFilledLog({kHash, kHash, padAddr(kMaker), padAddr(kTaker)}, good, &err).has_value());
    CHECK(err.find("signature") != std::string::npos);
    CHECK(!decodeOrderFilledLog({kOrderFilledTopic0, kHash, padAddr(kMaker)}, good, &err).has_value());
    CHECK(!decodeOrderFilledLog({kOrderFilledTopic0, kHash, padAddr(kMaker), padAddr(kTaker)}, good.substr(0, 100),
                                &err)
               .has_value());
    const std::string hugeAmount = "0x" + word(0) + word(1) + std::string(64, 'f') + word(3) + word(4);
    CHECK(!decodeOrderFilledLog({kOrderFilledTopic0, kHash, padAddr(kMaker), padAddr(kTaker)}, hugeAmount, &err)
               .has_value());
}

// ---------- normalization ----------

TEST_CASE(normalize_maker_buying_is_bid) {
    const auto f = normalize(event("0", kYes, 5'200'000, 10'000'000));
    REQUIRE(f.has_value());
    CHECK_EQ(f->makerSide, Side::Bid);
    CHECK_EQ(f->assetId, kYes);
    CHECK_EQ(f->price, 520'000); // 0.52 USDC
    CHECK_EQ(f->qty, 10'000'000u);
    CHECK_EQ(f->notional, 5'200'000u);
}

TEST_CASE(normalize_maker_selling_is_ask) {
    const auto f = normalize(event(kYes, "0", 4'000'000, 2'120'000));
    REQUIRE(f.has_value());
    CHECK_EQ(f->makerSide, Side::Ask);
    CHECK_EQ(f->price, 530'000);
    CHECK_EQ(f->qty, 4'000'000u);
}

TEST_CASE(normalize_rounds_price_to_nearest_micro) {
    // 1 USDC for 3 shares = 0.3333333.. -> 333333 ; 2 USDC for 3 shares -> 666667
    CHECK_EQ(normalize(event("0", kYes, 1'000'000, 3'000'000))->price, 333'333);
    CHECK_EQ(normalize(event("0", kYes, 2'000'000, 3'000'000))->price, 666'667);
}

TEST_CASE(normalize_rejects_invalid_events) {
    std::string err;
    CHECK(!normalize(event(kYes, kYes, 1, 1), &err).has_value());   // no USDC leg
    CHECK(!normalize(event("0", "0", 1, 1), &err).has_value());     // both USDC
    CHECK(!normalize(event("0", kYes, 0, 10), &err).has_value());   // zero amount
    CHECK(!normalize(event("0", kYes, 3'000'000, 1'000'000), &err).has_value()); // price 3 USDC > 1
    CHECK(!err.empty());
}

// ---------- CSV + replay ----------

TEST_CASE(csv_decoded_format_snake_case_and_comments) {
    std::istringstream in(
        "# comment line\n"
        "timestamp,order_hash,maker,taker,maker_asset_id,taker_asset_id,maker_amount_filled,taker_amount_filled,fee\r\n"
        "10," + kHash + "," + kMaker + "," + kTaker + ",0," + kYes + ",5200000,10000000,0\r\n"
        "\n"
        "11,\"" + kHash + "\"," + kMaker + "," + kTaker + "," + kYes + ",0,4000000,2120000,0\n");
    IngestStats stats;
    const auto events = readOrderFilledCsv(in, stats);
    CHECK(stats.errors.empty());
    REQUIRE(events.size() == 2);
    CHECK_EQ(events[0].timestamp, 10);
    CHECK_EQ(events[1].makerAssetId, kYes);
    CHECK_EQ(events[1].takerAmountFilled, 2'120'000u);
}

TEST_CASE(csv_bad_rows_are_counted_not_fatal) {
    std::istringstream in(
        "orderHash,maker,taker,makerAssetId,takerAssetId,makerAmountFilled,takerAmountFilled\n"
        "0x12," + kMaker + "," + kTaker + ",0,1,1,1\n"                 // bad hash
        + kHash + ",0xnothex," + kTaker + ",0,1,1,1\n"                 // bad maker
        + kHash + "," + kMaker + "," + kTaker + ",0,1,-5,1\n"          // bad amount
        + kHash + "," + kMaker + "," + kTaker + ",0,1,500000,1000000\n"); // ok
    IngestStats stats;
    const auto events = readOrderFilledCsv(in, stats);
    CHECK_EQ(stats.rowsRead, 4u);
    CHECK_EQ(stats.invalidRows, 3u);
    CHECK_EQ(events.size(), 1u);
}

TEST_CASE(csv_missing_required_column_fails_cleanly) {
    std::istringstream in("orderHash,maker,taker\n" + kHash + "," + kMaker + "," + kTaker + "\n");
    IngestStats stats;
    CHECK(readOrderFilledCsv(in, stats).empty());
    REQUIRE(!stats.errors.empty());
    CHECK(stats.errors[0].find("missing required column") != std::string::npos);
}

TEST_CASE(replayer_reproduces_fill_and_summarizes) {
    FillReplayer rep;
    IngestStats stats;
    CHECK(rep.apply(event("0", kYes, 5'200'000, 10'000'000, 100), stats));
    CHECK(rep.apply(event(kYes, "0", 4'000'000, 2'120'000, 200), stats));
    CHECK_EQ(stats.fillsReplayed, 2u);
    CHECK_EQ(stats.engineMismatches, 0u);

    const auto& s = rep.summaries().at(kYes);
    CHECK_EQ(s.trades, 2u);
    CHECK_EQ(s.volume, 14'000'000u);
    CHECK_EQ(s.notional, 7'320'000u);
    CHECK_EQ(s.open, 520'000);
    CHECK_EQ(s.last, 530'000);
    CHECK_EQ(s.high, 530'000);
    CHECK_EQ(s.low, 520'000);
    CHECK_EQ(s.vwap, 522'857); // 7.32 / 14 = 0.522857..
    CHECK_EQ(s.firstTimestamp, 100);
    CHECK_EQ(s.lastTimestamp, 200);

    const OrderBook* book = rep.book(kYes);
    REQUIRE(book != nullptr);
    CHECK(book->empty()); // every replayed fill fully matches
    CHECK_INVARIANTS(*book);
}

TEST_CASE(replayer_skips_exchange_taker_leg_by_default) {
    IngestStats stats;
    FillReplayer rep;
    CHECK(!rep.apply(event("0", kYes, 1'000'000, 2'000'000, 1, kCtfExchange), stats));
    CHECK(!rep.apply(event("0", kYes, 1'000'000, 2'000'000, 1, "0xC5D563A36AE78145C45A50134D48A1215220F80A"), stats));
    CHECK_EQ(stats.skippedExchangeLegs, 2u);
    CHECK(rep.summaries().empty());

    FillReplayer keepAll(FillReplayer::Options{false});
    IngestStats s2;
    CHECK(keepAll.apply(event("0", kYes, 1'000'000, 2'000'000, 1, kCtfExchange), s2));
    CHECK_EQ(s2.fillsReplayed, 1u);
}

TEST_CASE(replayer_writes_trade_log) {
    std::ostringstream log;
    FillReplayer rep;
    rep.setTradeLog(&log);
    IngestStats stats;
    rep.apply(event("0", kYes, 5'200'000, 10'000'000, 100), stats);
    CHECK_EQ(log.str(), "100," + kYes + ",ASK,0.520000,10.000000,1,2," + kMaker + "," + kTaker + "\n");
}

TEST_CASE(ingest_sorts_by_timestamp) {
    std::istringstream in(
        "timestamp,orderHash,maker,taker,makerAssetId,takerAssetId,makerAmountFilled,takerAmountFilled\n"
        "300," + kHash + "," + kMaker + "," + kTaker + ",0," + kYes + ",3000000,10000000\n"
        "100," + kHash + "," + kMaker + "," + kTaker + ",0," + kYes + ",1000000,10000000\n"
        "200," + kHash + "," + kMaker + "," + kTaker + ",0," + kYes + ",2000000,10000000\n");
    FillReplayer rep;
    const auto stats = ingestCsv(in, rep);
    CHECK_EQ(stats.fillsReplayed, 3u);
    const auto& s = rep.summaries().at(kYes);
    CHECK_EQ(s.open, 100'000);
    CHECK_EQ(s.last, 300'000);
}

TEST_CASE(sample_files_decoded_and_raw_agree) {
    auto run = [](const std::string& file) {
        std::ifstream in(std::string(OBS_SOURCE_DIR) + "/data/" + file);
        FillReplayer rep;
        auto stats = ingestCsv(in, rep);
        return std::make_pair(stats, rep.summaries());
    };
    const auto decoded = run("polymarket_sample_fills.csv");
    const auto raw = run("polymarket_sample_rawlogs.csv");
    CHECK_EQ(decoded.first.eventsParsed, 7u);
    CHECK_EQ(decoded.first.invalidRows, 0u);
    CHECK_EQ(decoded.first.skippedExchangeLegs, 1u);
    CHECK_EQ(decoded.first.fillsReplayed, 6u);
    CHECK_EQ(decoded.first.engineMismatches, 0u);
    CHECK_EQ(raw.first.fillsReplayed, 6u);
    CHECK_EQ(raw.first.engineMismatches, 0u);
    REQUIRE(decoded.second.size() == 2);
    REQUIRE(raw.second.size() == 2);
    for (const auto& [id, s] : decoded.second) {
        REQUIRE(raw.second.count(id) == 1);
        const auto& r = raw.second.at(id);
        CHECK_EQ(s.volume, r.volume);
        CHECK_EQ(s.vwap, r.vwap);
        CHECK_EQ(s.trades, r.trades);
    }
    // hand-checked: YES = 5@.51 + 10@.52 + 4@.53 + 7.5@.54 = 13.92 USDC over 26.5 shares
    const auto& yes = decoded.second.at(kYes);
    CHECK_EQ(yes.volume, 26'500'000u);
    CHECK_EQ(yes.notional, 13'920'000u);
    CHECK_EQ(yes.vwap, 525'283);
}

TEST_CASE(format_helpers) {
    CHECK_EQ(formatPrice(520'000), std::string("0.520000"));
    CHECK_EQ(formatPrice(1'000'000), std::string("1.000000"));
    CHECK_EQ(formatShares(12'500'001), std::string("12.500001"));
}

TEST_MAIN()
