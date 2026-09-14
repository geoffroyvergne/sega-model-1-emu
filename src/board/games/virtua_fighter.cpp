#include "board/games/virtua_fighter.h"

namespace model1::board::games::virtua_fighter {

const RomRegion& romx() {
    static const RomRegion region{
        "ROMX",
        RomLayout::Interleaved16,
        {
            {"epr-16082.14", 0x80000, 0xb23f22eeu},
            {"epr-16083.15", 0x80000, 0xd12c77f8u},
        },
        0x200000,
    };
    return region;
}

const RomRegion& rom0_low() {
    static const RomRegion region{
        "ROM0-low",
        RomLayout::Plain,
        {
            {"epr-16080.4", 0x20000, 0x3662e1a5u},
        },
        0xfc0000,
    };
    return region;
}

const RomRegion& rom0_high() {
    static const RomRegion region{
        "ROM0-high",
        RomLayout::Plain,
        {
            {"epr-16081.5", 0x20000, 0x6dec06ceu},
        },
        0xfe0000,
    };
    return region;
}

const std::array<RomRegion, 4>& romo_banks() {
    static const std::array<RomRegion, 4> banks{
        RomRegion{
            "ROMO-bank0",
            RomLayout::Interleaved16,
            {
                {"mpr-16084.6", 0x80000, 0x483f453bu},
                {"mpr-16085.7", 0x80000, 0x5fa01277u},
            },
            0x100000,
        },
        RomRegion{
            "ROMO-bank1",
            RomLayout::Interleaved16,
            {
                {"mpr-16086.8", 0x80000, 0xdeac47a1u},
                {"mpr-16087.9", 0x80000, 0x7a64daacu},
            },
            0x100000,
        },
        RomRegion{
            "ROMO-bank2",
            RomLayout::Interleaved16,
            {
                {"mpr-16088.10", 0x80000, 0xfcda2d1eu},
                {"mpr-16089.11", 0x80000, 0x39befbe0u},
            },
            0x100000,
        },
        RomRegion{
            "ROMO-bank3",
            RomLayout::Interleaved16,
            {
                {"mpr-16090.12", 0x80000, 0x90c76831u},
                {"mpr-16091.13", 0x80000, 0x53115448u},
            },
            0x100000,
        },
    };
    return banks;
}

} // namespace model1::board::games::virtua_fighter
