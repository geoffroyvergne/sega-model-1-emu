#include "board/rom_set.h"

#include <array>
#include <fstream>

namespace model1::board {

namespace {

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw RomLoadError("could not open ROM file: " + path);
    }
    const std::streamsize size = in.tellg();
    in.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (size > 0 && !in.read(reinterpret_cast<char*>(data.data()), size)) {
        throw RomLoadError("failed reading ROM file: " + path);
    }
    return data;
}

// Standard CRC-32 (polynomial 0xEDB88320, the same one used by zip/PNG/
// MAME's own ROM verification) -- a public, well-known algorithm, not
// copied from any ROM-specific source.
const std::array<uint32_t, 256>& crc32_table() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    return table;
}

void validate_file(const std::string& directory, const RomFile& file, const std::vector<uint8_t>& data) {
    if (data.size() != file.size) {
        throw RomLoadError(
            "ROM file " + file.filename + " in " + directory + " is " + std::to_string(data.size()) +
            " bytes, expected " + std::to_string(file.size));
    }
    if (file.crc32.has_value()) {
        const uint32_t actual = crc32(data.data(), data.size());
        if (actual != *file.crc32) {
            throw RomLoadError("ROM file " + file.filename + " in " + directory + " has the wrong checksum");
        }
    }
}

} // namespace

uint32_t crc32(const uint8_t* data, size_t length) {
    const auto& table = crc32_table();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; i++) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::vector<uint8_t> load_rom_region(const std::string& directory, const RomRegion& region) {
    std::vector<std::vector<uint8_t>> files;
    files.reserve(region.files.size());
    for (const RomFile& file : region.files) {
        std::vector<uint8_t> data = read_file(directory + "/" + file.filename);
        validate_file(directory, file, data);
        files.push_back(std::move(data));
    }

    switch (region.layout) {
        case RomLayout::Plain: {
            if (files.size() != 1) {
                throw RomLoadError("region " + region.name + ": Plain layout requires exactly one file");
            }
            return files[0];
        }
        case RomLayout::Interleaved16: {
            if (files.size() != 2) {
                throw RomLoadError("region " + region.name + ": Interleaved16 layout requires exactly two files");
            }
            if (files[0].size() != files[1].size()) {
                throw RomLoadError("region " + region.name + ": Interleaved16 files must be the same size");
            }
            const size_t half = files[0].size();
            std::vector<uint8_t> result(half * 2);
            for (size_t i = 0; i < half; i++) {
                result[i * 2] = files[0][i];
                result[i * 2 + 1] = files[1][i];
            }
            return result;
        }
    }
    throw RomLoadError("region " + region.name + ": unknown layout");
}

} // namespace model1::board
