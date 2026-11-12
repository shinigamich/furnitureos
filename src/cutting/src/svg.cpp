// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/cutting/svg.hpp"

#include "fos/core/units.hpp"

#include <algorithm>
#include <map>

namespace fos::cutting {
namespace {

using core::formatLength;

std::string num(double v) { return formatLength(v, 3); }

std::string sizeText(double l, double w) { return formatLength(l) + " \xC3\x97 " + formatLength(w); }

/// <rect> in SVG coordinates for a sheet-frame rectangle (Y flipped: sheet origin bottom-left).
std::string rectTag(const Rect& r, double sheetWidth, const char* cls, const std::string& extra = {}) {
    return std::string("<rect class=\"") + cls + "\" x=\"" + num(r.x) + "\" y=\"" + num(sheetWidth - r.y - r.width) +
           "\" width=\"" + num(r.length) + "\" height=\"" + num(r.width) + "\"" + extra + "/>\n";
}

/// Centred text; turned by -90 degrees when the rectangle is taller than long.
std::string textTag(const Rect& r, double sheetWidth, double dy, double fontSize, const std::string& text,
                    const char* cls) {
    const double cx = r.x + r.length / 2;
    const double cy = sheetWidth - r.y - r.width / 2;
    std::string t = std::string("<text class=\"") + cls + "\" font-size=\"" + num(fontSize) + "\" x=\"" + num(cx) +
                    "\" y=\"" + num(cy + dy) + "\"";
    if (r.width > r.length) t += " transform=\"rotate(-90 " + num(cx) + " " + num(cy) + ")\"";
    return t + ">" + xmlEscape(text) + "</text>\n";
}

/// Font size (mm) so that `chars` characters fit the longer side and two lines the shorter.
double fitFont(const Rect& r, std::size_t chars) {
    const double along = std::max(r.length, r.width), across = std::min(r.length, r.width);
    const double byLength = along / (0.62 * static_cast<double>(std::max<std::size_t>(chars, 1)));
    return std::clamp(std::min(byLength, across / 2.8), 4.0, 48.0);
}

/// Number of UTF-8 code points (for a rough text width estimate).
std::size_t codePoints(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char ch : s)
        if ((ch & 0xC0u) != 0x80u) ++n;
    return n;
}

} // namespace

std::string xmlEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        switch (ch) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default:
            // Control characters other than tab/newline are not allowed in XML 1.0.
            if (static_cast<unsigned char>(ch) < 0x20 && ch != '\t' && ch != '\n' && ch != '\r')
                out += ' ';
            else
                out += ch;
        }
    }
    return out;
}

std::string renderSheetSvg(const Job& job, const SheetLayout& sheet, const SvgOptions& options) {
    const double L = sheet.sheetLength, W = sheet.sheetWidth;
    const double legendFont = std::clamp(L / 70.0, 8.0, 40.0);
    const double legendHeight = options.legend ? legendFont * 4.6 : 0.0;
    std::map<std::string, const Piece*> pieces;
    for (const Piece& p : job.pieces) pieces.emplace(p.id, &p);

    std::string s;
    s += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    s += "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + num(L) + "mm\" height=\"" + num(W + legendHeight) +
         "mm\" viewBox=\"0 0 " + num(L) + " " + num(W + legendHeight) + "\">\n";
    s += "<title>" + xmlEscape("Cutting layout " + job.name + " - " + sheet.stockId) + "</title>\n";
    s += "<defs>\n<pattern id=\"fos-hatch\" width=\"24\" height=\"24\" patternUnits=\"userSpaceOnUse\" "
         "patternTransform=\"rotate(45)\"><line x1=\"0\" y1=\"0\" x2=\"0\" y2=\"24\" stroke=\"#9a9a9a\" "
         "stroke-width=\"3\"/></pattern>\n</defs>\n";
    s += "<style>\n"
         ".fos-trim{fill:#c9b79c;stroke:#333;stroke-width:2}\n"
         ".fos-usable{fill:#ffffff;stroke:none}\n"
         ".fos-piece{fill:#f3dfb0;stroke:#5a4520;stroke-width:1.5}\n"
         ".fos-kerf{fill:#d62828;stroke:none}\n"
         ".fos-remnant{fill:#bfe3c0;stroke:#2d6a4f;stroke-width:1.5;stroke-dasharray:12 6}\n"
         ".fos-waste{fill:url(#fos-hatch);stroke:#9a9a9a;stroke-width:1}\n"
         ".fos-label{font-family:sans-serif;text-anchor:middle;dominant-baseline:central;fill:#1d1d1d}\n"
         ".fos-legend{font-family:sans-serif;fill:#1d1d1d}\n"
         "</style>\n";

    // Sheet (trim colour shows through around the usable area).
    s += rectTag(Rect{0, 0, L, W}, W, "fos-trim");
    s += rectTag(sheet.usable, W, "fos-usable");

    for (const Offcut& o : sheet.offcuts) {
        s += rectTag(o.rect, W, o.reusable ? "fos-remnant" : "fos-waste");
        if (o.reusable && options.offcutSizes) {
            const std::string text = sizeText(o.rect.length, o.rect.width);
            s += textTag(o.rect, W, 0, fitFont(o.rect, codePoints(text)), text, "fos-label");
        }
    }
    // Kerf strips of the split cuts.
    for (const Cut& c : sheet.cuts) {
        if (c.kind != CutKind::Split || c.kerfWidth <= 0) continue;
        const Rect k = c.orientation == CutOrientation::Crosswise
                           ? Rect{c.position, c.source.y, c.kerfWidth, c.source.width}
                           : Rect{c.source.x, c.position, c.source.length, c.kerfWidth};
        s += rectTag(k, W, "fos-kerf");
    }
    for (const Placement& p : sheet.placements) {
        const Rect r{p.x, p.y, p.length, p.width};
        const auto it = pieces.find(p.pieceId);
        const std::string label =
            it != pieces.end() && !it->second->label.empty() ? it->second->label : p.pieceId;
        s += "<g class=\"fos-piece-group\" data-piece=\"" + xmlEscape(p.pieceId) + "\" data-instance=\"" +
             std::to_string(p.instance) + "\" data-rotated=\"" + (p.rotated ? "true" : "false") + "\">\n";
        s += rectTag(r, W, "fos-piece");
        if (options.pieceLabels) {
            // U+21BB marks a piece turned by 90 degrees relative to its definition.
            const std::string line1 = (p.rotated ? std::string("\xE2\x86\xBB ") : std::string()) + label;
            const std::string line2 = sizeText(p.length, p.width);
            const double font = fitFont(r, std::max(codePoints(line1), codePoints(line2)));
            s += textTag(r, W, -0.6 * font, font, line1, "fos-label");
            s += textTag(r, W, 0.6 * font, font, line2, "fos-label");
        }
        s += "</g>\n";
    }

    if (options.legend) {
        const double f = legendFont;
        const double y0 = W + f * 0.4;
        const std::string title = job.name + (job.name.empty() ? "" : " \xE2\x80\x94 ") + "stock " + sheet.stockId + " " +
                                  sizeText(L, W) + " mm, kerf " + formatLength(job.kerf) + " mm, utilisation " +
                                  formatLength(sheet.utilisationOfSheetPct) + " % of sheet / " +
                                  formatLength(sheet.utilisationOfUsablePct) + " % of usable area";
        s += "<g class=\"fos-legend\">\n";
        // Shrink the title so that it stays within the sheet length (about 0.55 em per character).
        const double titleFont = std::min(f, L / (0.55 * static_cast<double>(codePoints(title))));
        s += "<text font-size=\"" + num(titleFont) + "\" x=\"0\" y=\"" + num(y0 + f) + "\">" + xmlEscape(title) +
             "</text>\n";
        const char* const entries[][2] = {{"fos-piece", "Piece (\xE2\x86\xBB = rotated)"},
                                          {"fos-kerf", "Saw kerf"},
                                          {"fos-remnant", "Reusable remnant"},
                                          {"fos-waste", "Waste"},
                                          {"fos-trim", "Edge trim"}};
        const double step = L / 5.0;
        const double yBox = y0 + f * 1.9;
        for (std::size_t i = 0; i < 5; ++i) {
            const double x = static_cast<double>(i) * step;
            s += std::string("<rect class=\"") + entries[i][0] + "\" x=\"" + num(x + 1) + "\" y=\"" + num(yBox) +
                 "\" width=\"" + num(f * 1.8) + "\" height=\"" + num(f * 1.2) + "\"/>\n";
            s += "<text font-size=\"" + num(f * 0.9) + "\" x=\"" + num(x + f * 2.2) + "\" y=\"" + num(yBox + f) +
                 "\">" + xmlEscape(entries[i][1]) + "</text>\n";
        }
        s += "</g>\n";
    }
    s += "</svg>\n";
    return s;
}

} // namespace fos::cutting
