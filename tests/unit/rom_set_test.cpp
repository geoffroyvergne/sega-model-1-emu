#include "doctest.h"
#include "board/rom_set.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace model1::board;

namespace {

// Writes a synthetic (non-copyrighted, test-only) file of the given bytes
// into the scratch temp directory, returning its containing directory.
class ScratchDir {
public:
    ScratchDir() {
        dir_ = std::filesystem::temp_directory_path() / "model1_rom_set_test";
        std::filesystem::create_directories(dir_);
    }
    ~ScratchDir() { std::filesystem::remove_all(dir_); }

    std::string path() const { return dir_.string(); }

    void write(const std::string& filename, const std::vector<uint8_t>& bytes) const {
        std::ofstream out(dir_ / filename, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

private:
    std::filesystem::path dir_;
};

} // namespace

TEST_CASE("crc32 matches the standard algorithm for a known test vector") {
    // "123456789" is the canonical CRC-32 test vector; the correct result
    // (0xCBF43926) is documented wherever the algorithm itself is (e.g.
    // the PNG and zlib specifications) -- not tied to any ROM.
    const std::string input = "123456789";
    CHECK(crc32(reinterpret_cast<const uint8_t*>(input.data()), input.size()) == 0xCBF43926u);
}

TEST_CASE("load_rom_region with Plain layout loads a single file's bytes") {
    ScratchDir dir;
    dir.write("test.rom", {0x11, 0x22, 0x33, 0x44});

    RomRegion region{"TEST", RomLayout::Plain, {{"test.rom", 4, std::nullopt}}, 0};
    std::vector<uint8_t> data = load_rom_region(dir.path(), region);

    CHECK(data == std::vector<uint8_t>{0x11, 0x22, 0x33, 0x44});
}

TEST_CASE("load_rom_region with Interleaved16 layout interleaves two files byte-by-byte") {
    ScratchDir dir;
    dir.write("even.rom", {0xaa, 0xbb});
    dir.write("odd.rom", {0xcc, 0xdd});

    RomRegion region{
        "TEST",
        RomLayout::Interleaved16,
        {{"even.rom", 2, std::nullopt}, {"odd.rom", 2, std::nullopt}},
        0,
    };
    std::vector<uint8_t> data = load_rom_region(dir.path(), region);

    CHECK(data == std::vector<uint8_t>{0xaa, 0xcc, 0xbb, 0xdd});
}

TEST_CASE("load_rom_region validates file size and throws on mismatch") {
    ScratchDir dir;
    dir.write("wrong_size.rom", {0x01, 0x02});

    RomRegion region{"TEST", RomLayout::Plain, {{"wrong_size.rom", 4, std::nullopt}}, 0};

    CHECK_THROWS_AS(load_rom_region(dir.path(), region), RomLoadError);
}

TEST_CASE("load_rom_region validates checksum when provided and throws on mismatch") {
    ScratchDir dir;
    dir.write("test.rom", {0x11, 0x22, 0x33, 0x44});

    RomRegion region{"TEST", RomLayout::Plain, {{"test.rom", 4, 0xdeadbeefu}}, 0};

    CHECK_THROWS_AS(load_rom_region(dir.path(), region), RomLoadError);
}

TEST_CASE("load_rom_region accepts a correct checksum") {
    ScratchDir dir;
    const std::vector<uint8_t> bytes = {0x11, 0x22, 0x33, 0x44};
    dir.write("test.rom", bytes);
    const uint32_t correct_crc = crc32(bytes.data(), bytes.size());

    RomRegion region{"TEST", RomLayout::Plain, {{"test.rom", 4, correct_crc}}, 0};
    std::vector<uint8_t> data = load_rom_region(dir.path(), region);

    CHECK(data == bytes);
}

TEST_CASE("load_rom_region throws when a file is missing") {
    ScratchDir dir;
    RomRegion region{"TEST", RomLayout::Plain, {{"does_not_exist.rom", 4, std::nullopt}}, 0};

    CHECK_THROWS_AS(load_rom_region(dir.path(), region), RomLoadError);
}
