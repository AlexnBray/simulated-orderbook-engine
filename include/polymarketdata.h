#pragma once
#include "pch.h"



using Address = std::uint8_t;
using AssetId = std::uint64_t;

#pragma pack(push, 1)

struct PolymarketOrderFilledEvent {

    Address maker_address[32];
    Address taker_address[32];
}