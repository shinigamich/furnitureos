// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/core/units.hpp"

#include <cstdio>

namespace fos::core {

double roundTo(double value, int decimals) noexcept {
    const double scale = std::pow(10.0, decimals);
    return std::round(value * scale) / scale;
}

std::string formatLength(double mm, int decimals) {
    if (decimals < 0) decimals = 0;
    if (decimals > 9) decimals = 9;
    double r = roundTo(mm, decimals);
    if (r == 0.0) r = 0.0; // normalise -0
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, r);
    std::string s(buf);
    for (char& c : s)
        if (c == ',') c = '.'; // guard against a non-"C" LC_NUMERIC
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

} // namespace fos::core
