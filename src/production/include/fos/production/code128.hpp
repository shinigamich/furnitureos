// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace fos::production {

/// Symbol values of Code 128 (ISO/IEC 15417) used by the subset-B encoder.
inline constexpr int kCode128StartB = 104;
inline constexpr int kCode128Stop = 106;
/// Minimum quiet zone on each side of a Code 128 symbol, in modules (ISO/IEC 15417: 10 X).
inline constexpr int kCode128QuietZoneModules = 10;

/// A Code 128 symbol encoded entirely in code set B (printable ASCII 32..126).
struct Code128Symbol {
    /// Symbol values in print order: Start B (104), one value per character (char - 32),
    /// the checksum, Stop (106).
    std::vector<int> values;
    /// Weighted modulo-103 checksum: (104 + sum_i i * value_i) mod 103, i = 1-based position.
    int checksum = 0;
    /// Element widths in modules, alternating bar, space, bar, ... starting and ending with a
    /// bar (6 elements per symbol character, 7 for Stop including its final 2-module bar).
    /// Quiet zones are NOT included.
    std::vector<int> widths;
    /// Sum of `widths` = 11 * (characters + 2) + 13.
    int modules() const noexcept;
};

/// The 6 (or 7 for the Stop, value 106) bar/space widths of one Code 128 symbol value 0..106,
/// e.g. "212222" for value 0. Empty for values outside 0..106.
std::string_view code128Pattern(int value) noexcept;

/// Encodes `text` in Code 128 code set B with checksum. Error (InvalidArgument) for empty text or
/// any byte outside printable ASCII 32..126 (names the offending position and byte); code sets A/C,
/// shift and FNC characters are not used.
core::Result<Code128Symbol> encodeCode128B(std::string_view text);

} // namespace fos::production
