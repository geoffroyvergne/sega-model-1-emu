#include "board/games/virtua_racing.h"

namespace model1::board::games::virtua_racing {

const RomRegion& romx() {
    static const RomRegion region{
        "ROMX",
        RomLayout::Interleaved16,
        {
            {"epr-14882.14", 0x80000, 0x547d75adu},
            {"epr-14883.15", 0x80000, 0x6bfad8b1u},
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
            {"epr-14878a.4", 0x20000, 0x6d69e695u},
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
            {"epr-14879a.5", 0x20000, 0xd45af9ddu},
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
                {"mpr-14880.6", 0x80000, 0xadc7c208u},
                {"mpr-14881.7", 0x80000, 0xe5ab89dfu},
            },
            0x100000,
        },
        RomRegion{
            "ROMO-bank1",
            RomLayout::Interleaved16,
            {
                {"mpr-14884.8", 0x80000, 0x6cf9c026u},
                {"mpr-14885.9", 0x80000, 0xf65c9262u},
            },
            0x100000,
        },
        RomRegion{
            "ROMO-bank2",
            RomLayout::Interleaved16,
            {
                {"mpr-14886.10", 0x80000, 0x92868734u},
                {"mpr-14887.11", 0x80000, 0x10c7c636u},
            },
            0x100000,
        },
        RomRegion{
            "ROMO-bank3",
            RomLayout::Interleaved16,
            {
                {"mpr-14888.12", 0x80000, 0x04bfdc5bu},
                {"mpr-14889.13", 0x80000, 0xc49f0486u},
            },
            0x100000,
        },
    };
    return banks;
}

} // namespace model1::board::games::virtua_racing
