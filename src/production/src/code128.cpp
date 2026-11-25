// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/code128.hpp"

#include <array>
#include <cstdio>

namespace fos::production {
namespace {

// ISO/IEC 15417 symbol character table: bar, space, bar, space, bar, space widths in modules.
// Every pattern of 0..105 spans 11 modules with an even bar total; the Stop spans 13.
constexpr std::array<std::string_view, 107> kPatterns = {
    "212222", "222122", "222221", "121223", "121322", "131222", "122213", "122312", "132212", "221213", // 0-9
    "221312", "231212", "112232", "122132", "122231", "113222", "123122", "123221", "223211", "221132", // 10-19
    "221231", "213212", "223112", "312131", "311222", "321122", "321221", "312212", "322112", "322211", // 20-29
    "212123", "212321", "232121", "111323", "131123", "131321", "112313", "132113", "132311", "211313", // 30-39
    "231113", "231311", "112133", "112331", "132131", "113123", "113321", "133121", "313121", "211331", // 40-49
    "231131", "213113", "213311", "213131", "311123", "311321", "331121", "312113", "312311", "332111", // 50-59
    "314111", "221411", "431111", "111224", "111422", "121124", "121421", "141122", "141221", "112214", // 60-69
    "112412", "122114", "122411", "142112", "142211", "241211", "221114", "413111", "241112", "134111", // 70-79
    "111242", "121142", "121241", "114212", "124112", "124211", "411212", "421112", "421211", "212141", // 80-89
    "214121", "412121", "111143", "111341", "131141", "114113", "114311", "411113", "411311", "113141", // 90-99
    "114131", "311141", "411131", "211412", "211214", "211232", "2331112"};                          // 100-106

} // namespace

int Code128Symbol::modules() const noexcept {
    int sum = 0;
    for (int w : widths) sum += w;
    return sum;
}

std::string_view code128Pattern(int value) noexcept {
    if (value < 0 || value > kCode128Stop) return {};
    return kPatterns[static_cast<std::size_t>(value)];
}

core::Result<Code128Symbol> encodeCode128B(std::string_view text) {
    if (text.empty())
        return core::makeError(core::ErrorCode::InvalidArgument, "Code 128 cannot encode an empty text.");
    Code128Symbol s;
    s.values.reserve(text.size() + 3);
    s.values.push_back(kCode128StartB);
    long weighted = kCode128StartB;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if (byte < 32 || byte > 126) {
            char hex[8];
            std::snprintf(hex, sizeof hex, "0x%02X", static_cast<unsigned>(byte));
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Code 128 subset B cannot encode byte " + std::string(hex) + " at position " +
                                       std::to_string(i + 1) + " of '" + std::string(text) +
                                       "'; only printable ASCII (32..126) is supported.");
        }
        const int value = static_cast<int>(byte) - 32;
        s.values.push_back(value);
        weighted += static_cast<long>(i + 1) * value;
    }
    s.checksum = static_cast<int>(weighted % 103);
    s.values.push_back(s.checksum);
    s.values.push_back(kCode128Stop);
    for (int v : s.values)
        for (char c : code128Pattern(v)) s.widths.push_back(c - '0');
    return s;
}

} // namespace fos::production
