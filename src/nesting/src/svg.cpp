// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/nesting/svg.hpp"

#include "fos/core/units.hpp"
#include "fos/nesting/nest.hpp"

#include <algorithm>
#include <string>

namespace fos::nesting {

std::string xmlEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default: out += c;
        }
    }
    return out;
}

namespace {

std::string num(double v) { return core::formatLength(v, 3); }

std::string rect(double x, double y, double w, double h, double sheetW, const char* cls) {
    return "<rect class=\"" + std::string(cls) + "\" x=\"" + num(x) + "\" y=\"" + num(sheetW - y - h) + "\" width=\"" +
           num(w) + "\" height=\"" + num(h) + "\"/>\n";
}

} // namespace

std::string renderNestSvg(const Job& job, const SheetLayout& layout) {
    const Sheet* sheet = nullptr;
    for (const Sheet& s : job.sheets)
        if (s.id == layout.sheetId) sheet = &s;
    Sheet fallback;
    if (!sheet) {
        fallback.id = layout.sheetId;
        fallback.length = std::max(layout.usedLength, 1.0);
        fallback.width = 1.0;
        for (const auto& p : layout.placements)
            for (auto v : p.outline.vertices) fallback.width = std::max(fallback.width, v.y);
        sheet = &fallback;
    }
    const double L = sheet->length, W = sheet->width;
    std::string s;
    s += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    s += "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + num(L) + "mm\" height=\"" + num(W) +
         "mm\" viewBox=\"0 0 " + num(L) + " " + num(W) + "\">\n";
    s += "<title>" + xmlEscape(sheet->label.empty() ? sheet->id : sheet->label) + " #" + std::to_string(layout.copy) +
         " (heuristic nesting)</title>\n";
    s += "<style>.fos-sheet{fill:#f4efe6;stroke:#333;stroke-width:1}.fos-margin{fill:#d9d2c5}"
         ".fos-area{fill:none;stroke:#888;stroke-width:0.5;stroke-dasharray:4 4}"
         ".fos-part{fill:#9cc3e6;stroke:#1f4e79;stroke-width:0.8}.fos-remnant{fill:#c8e6c9;fill-opacity:0.6;"
         "stroke:#2e7d32;stroke-width:0.8;stroke-dasharray:8 4}.fos-label{font-family:sans-serif;font-size:" +
         num(std::clamp(std::min(L, W) / 40.0, 4.0, 40.0)) + "px;text-anchor:middle;fill:#111}</style>\n";
    s += rect(0, 0, L, W, W, "fos-sheet");
    const auto& m = sheet->margins;
    if (m.left > 0) s += rect(0, 0, m.left, W, W, "fos-margin");
    if (m.right > 0) s += rect(L - m.right, 0, m.right, W, W, "fos-margin");
    if (m.bottom > 0) s += rect(0, 0, L, m.bottom, W, "fos-margin");
    if (m.top > 0) s += rect(0, W - m.top, L, m.top, W, "fos-margin");
    const auto area = placementArea(job, *sheet);
    if (!area.empty()) s += rect(area.min.x, area.min.y, area.width(), area.height(), W, "fos-area");
    if (layout.hasRemnant)
        s += rect(layout.remnant.min.x, layout.remnant.min.y, layout.remnant.width(), layout.remnant.height(), W,
                  "fos-remnant");
    for (const Placement& p : layout.placements) {
        std::string label = p.partId;
        for (const Part& part : job.parts)
            if (part.id == p.partId && !part.label.empty()) label = part.label;
        std::string d;
        for (std::size_t i = 0; i < p.outline.size(); ++i)
            d += (i ? " L " : "M ") + num(p.outline[i].x) + " " + num(W - p.outline[i].y);
        s += "<path class=\"fos-part\" d=\"" + d + " Z\"><title>" + xmlEscape(label) + " #" +
             std::to_string(p.instance) + "</title></path>\n";
        const auto c = geometry::centroid(p.outline);
        s += "<text class=\"fos-label\" x=\"" + num(c.x) + "\" y=\"" + num(W - c.y) + "\">" + xmlEscape(label) +
             "</text>\n";
    }
    s += "</svg>\n";
    return s;
}

} // namespace fos::nesting
