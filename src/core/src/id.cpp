// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/core/id.hpp"

#include <array>
#include <cstdint>
#include <mutex>
#include <random>

namespace fos::core {

std::string newUuid() {
    static std::mutex mutex;
    static std::mt19937_64 engine{[] {
        std::random_device rd;
        std::seed_seq seq{rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()};
        return std::mt19937_64(seq);
    }()};
    std::array<unsigned char, 16> bytes{};
    {
        std::lock_guard lock(mutex);
        for (std::size_t i = 0; i < 16; i += 8) {
            std::uint64_t r = engine();
            for (std::size_t j = 0; j < 8; ++j) bytes[i + j] = static_cast<unsigned char>(r >> (8 * j));
        }
    }
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40); // version 4
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80); // RFC 4122 variant
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        out.push_back(hex[bytes[i] >> 4]);
        out.push_back(hex[bytes[i] & 0x0F]);
    }
    return out;
}

} // namespace fos::core
