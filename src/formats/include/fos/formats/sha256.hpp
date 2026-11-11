// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fos::formats {

/// Incremental SHA-256 (FIPS 180-4). Own implementation, no external dependency.
/// Used for integrity checking of saved documents, not for security decisions.
class Sha256 {
public:
    Sha256() noexcept;
    /// Feed `size` bytes. May be called any number of times before finish().
    void update(const void* data, std::size_t size) noexcept;
    void update(std::string_view text) noexcept { update(text.data(), text.size()); }
    /// Completes the hash and returns the 32-byte digest. The object must not be
    /// updated afterwards (construct a new one for another message).
    std::array<std::uint8_t, 32> finish() noexcept;

private:
    void compress(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_;
    std::size_t buffered_ = 0;
    std::uint64_t totalBytes_ = 0;
};

/// SHA-256 of `data` as 64 lower-case hexadecimal characters.
std::string sha256Hex(std::string_view data);

} // namespace fos::formats
