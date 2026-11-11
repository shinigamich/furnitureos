// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/html.hpp"

namespace fos::production {

std::string escapeXml(std::string_view text) {
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default:
            if (c < 0x20 && c != '\t' && c != '\n' && c != '\r')
                out += ' ';
            else
                out += ch;
        }
    }
    return out;
}

} // namespace fos::production
