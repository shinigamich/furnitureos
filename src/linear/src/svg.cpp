// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/linear/svg.hpp"

#include "fos/core/units.hpp"

#include <algorithm>
#include <sstream>

namespace fos::linear {
namespace {

constexpr double kStrip = 40.0;  // mm, strip height
constexpr double kGap = 30.0;    // mm between strips (label line)

std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        case '\'': o += "&apos;"; break;
        default: o += c;
        }
    }
    return o;
}

std::string n(double v) {
    return core::formatLength(v, 3);
}

void rect(std::ostringstream& s, const char* cls, double x, double y, double w, double h) {
    if (w <= 0) return;
    s << "<rect class=\"" << cls << "\" x=\"" << n(x) << "\" y=\"" << n(y) << "\" width=\"" << n(w)
      << "\" height=\"" << n(h) << "\"/>\n";
}

} // namespace

std::string renderLinearSvg(const LinearJob& job, const LinearResult& result) {
    double width = 1;
    for (const auto& b : result.bars) width = std::max(width, b.stockLength);
    const double height = std::max(1.0, static_cast<double>(result.bars.size()) * (kStrip + kGap));
    std::ostringstream s;
    s << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << n(width) << "mm\" height=\"" << n(height)
      << "mm\" viewBox=\"0 0 " << n(width) << " " << n(height) << "\">\n"
      << "<style>.fos-bar{fill:#fff;stroke:#000;stroke-width:0.5}.fos-trim{fill:#999}"
         ".fos-piece{fill:#cfe0f5;stroke:#1f4e79;stroke-width:0.5}.fos-kerf{fill:#c00}"
         ".fos-offcut{fill:#bfe8bf}.fos-waste{fill:#e6e6e6}"
         ".fos-label{font-family:sans-serif;font-size:14px;fill:#000}</style>\n";
    for (std::size_t i = 0; i < result.bars.size(); ++i) {
        const auto& b = result.bars[i];
        const double y = static_cast<double>(i) * (kStrip + kGap) + kGap;
        s << "<text class=\"fos-label\" x=\"0\" y=\"" << n(y - 8) << "\">Bar " << i + 1 << ": " << esc(b.stockId)
          << " " << n(b.stockLength) << " mm, utilisation " << core::formatLength(100.0 * b.utilisation, 2)
          << " %</text>\n";
        rect(s, "fos-bar", 0, y, b.stockLength, kStrip);
        rect(s, "fos-trim", 0, y, b.trimStart, kStrip);
        rect(s, "fos-trim", b.stockLength - b.trimEnd, y, b.trimEnd, kStrip);
        double prevEnd = -1;
        for (const auto& c : b.cuts) {
            if (prevEnd >= 0) rect(s, "fos-kerf", prevEnd, y, c.start - prevEnd, kStrip);
            rect(s, "fos-piece", c.start, y, c.length, kStrip);
            std::string label = c.pieceId;
            for (const auto& p : job.pieces)
                if (p.id == c.pieceId && !p.label.empty()) label = p.label;
            s << "<text class=\"fos-label\" x=\"" << n(c.start + 4) << "\" y=\"" << n(y + kStrip / 2 + 5) << "\">"
              << esc(label) << " " << n(c.length) << "</text>\n";
            prevEnd = c.start + c.length;
        }
        const double usableEnd = b.stockLength - b.trimEnd;
        if (prevEnd >= 0 && b.offcutStart > prevEnd) rect(s, "fos-kerf", prevEnd, y, b.offcutStart - prevEnd, kStrip);
        if (b.offcutLength > 0)
            rect(s, b.offcutReusable ? "fos-offcut" : "fos-waste", b.offcutStart, y,
                 std::min(b.offcutLength, usableEnd - b.offcutStart), kStrip);
    }
    s << "</svg>\n";
    return s.str();
}

} // namespace fos::linear
