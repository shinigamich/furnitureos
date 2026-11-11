// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fos::drawings::detail {

/// Locale-independent number with up to `decimals` decimals, trailing zeros stripped,
/// never "-0". Non-finite values are written as 0.
std::string num(double v, int decimals = 4);

/// Decodes UTF-8 into code points; malformed bytes become U+FFFD.
std::vector<std::uint32_t> decodeUtf8(std::string_view s);

} // namespace fos::drawings::detail
