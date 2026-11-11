// SPDX-License-Identifier: AGPL-3.0-only
#include "format.hpp"

#include <cmath>
#include <cstdio>

namespace fos::drawings::detail {

std::string num(double v, int decimals) {
    if (!std::isfinite(v)) v = 0.0;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string s(buf);
    for (char& c : s)
        if (c == ',') c = '.';
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0" || s.empty()) s = "0";
    return s;
}

std::vector<std::uint32_t> decodeUtf8(std::string_view s) {
    std::vector<std::uint32_t> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const auto b0 = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = 0xFFFD;
        std::size_t len = 1;
        if (b0 < 0x80) {
            cp = b0;
        } else {
            std::size_t need = 0;
            std::uint32_t min = 0;
            if ((b0 & 0xE0) == 0xC0) { need = 1; cp = b0 & 0x1FU; min = 0x80; }
            else if ((b0 & 0xF0) == 0xE0) { need = 2; cp = b0 & 0x0FU; min = 0x800; }
            else if ((b0 & 0xF8) == 0xF0) { need = 3; cp = b0 & 0x07U; min = 0x10000; }
            bool ok = need > 0 && i + need < s.size(); // continuation bytes i+1 .. i+need exist
            if (ok) {
                for (std::size_t k = 1; k <= need; ++k) {
                    const auto b = static_cast<unsigned char>(s[i + k]);
                    if ((b & 0xC0) != 0x80) { ok = false; break; }
                    cp = (cp << 6) | (b & 0x3FU);
                }
            }
            if (ok && cp >= min && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) len = need + 1;
            else cp = 0xFFFD;
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

} // namespace fos::drawings::detail
