// SPDX-License-Identifier: AGPL-3.0-only
// Printable A4 job sheets (one set of pages per edge band) of the edge-banding job list.
#include "fos/production/edge_banding_jobs.hpp"

#include "fos/core/units.hpp"

#include <algorithm>
#include <set>

namespace fos::production {
namespace {

using core::formatLength;

constexpr double kPageW = 210, kPageH = 297, kMargin = 12;
constexpr double kRowsTop = 42, kRowH = 20, kFooter = 14;
constexpr double kDiagW = 40, kDiagH = 12; ///< Diagram box of a part row.
constexpr std::size_t kStripsPerLine = 5;

std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        default: o += c;
        }
    }
    return o;
}

std::string f(double v) { return formatLength(v, 2); }

std::string text(double x, double y, double size, const std::string& s, bool bold = false,
                 const char* anchor = "start") {
    return "<text x=\"" + f(x) + "\" y=\"" + f(y) + "\" font-size=\"" + f(size) + "\" text-anchor=\"" + anchor +
           "\"" + (bold ? " font-weight=\"bold\"" : "") + ">" + esc(s) + "</text>\n";
}

std::string line(double x1, double y1, double x2, double y2, double w) {
    return "<line x1=\"" + f(x1) + "\" y1=\"" + f(y1) + "\" x2=\"" + f(x2) + "\" y2=\"" + f(y2) +
           "\" stroke=\"#000\" stroke-width=\"" + f(w) + "\"/>\n";
}

/// Rectangle of the part (finished proportions) with its banded edges thick and step numbers.
/// Part frame: L1 at the bottom (y = 0), W1 on the left (x = 0).
std::string diagram(const PartBandingJob& p, double top) {
    const double l = p.length > 0 ? p.length : 1, w = p.width > 0 ? p.width : 1;
    const double s = std::min(kDiagW / l, kDiagH / w);
    const double dw = l * s, dh = w * s;
    const double x0 = kMargin + (kDiagW - dw) / 2, y0 = top + 4 + (kDiagH - dh) / 2;
    const double x1 = x0 + dw, y1 = y0 + dh;
    std::string o = "<rect x=\"" + f(x0) + "\" y=\"" + f(y0) + "\" width=\"" + f(dw) + "\" height=\"" + f(dh) +
                    "\" fill=\"none\" stroke=\"#000\" stroke-width=\"0.2\"/>\n";
    for (const auto& e : p.edges) {
        const std::string n = std::to_string(e.step);
        switch (e.side) {
        case domain::EdgeSide::L1:
            o += line(x0, y1, x1, y1, 1.0) + text((x0 + x1) / 2, y1 + 3.2, 2.6, n, true, "middle");
            break;
        case domain::EdgeSide::L2:
            o += line(x0, y0, x1, y0, 1.0) + text((x0 + x1) / 2, y0 - 1.0, 2.6, n, true, "middle");
            break;
        case domain::EdgeSide::W1:
            o += line(x0, y0, x0, y1, 1.0) + text(x0 - 1.2, (y0 + y1) / 2 + 1, 2.6, n, true, "end");
            break;
        case domain::EdgeSide::W2:
            o += line(x1, y0, x1, y1, 1.0) + text(x1 + 1.2, (y0 + y1) / 2 + 1, 2.6, n, true, "start");
            break;
        }
    }
    return o;
}

std::string edgeText(const BandedEdgeJob& e, int total) {
    return std::to_string(e.step) + "/" + std::to_string(total) + " " + domain::toString(e.side) + ": edge " +
           formatLength(e.edgeLength) + ", strip " + formatLength(e.stripLength) + " (cut edge " +
           formatLength(e.cutEdgeLength) + ")";
}

std::string partRow(const PartBandingJob& p, double top) {
    const double x = kMargin + kDiagW + 8;
    std::string o = diagram(p, top);
    o += text(x, top + 5, 3.4,
              "#" + std::to_string(p.partNumber) + " " + p.partName + " (" + p.partId.str() + ")  cut " +
                  formatLength(p.cutLength) + " x " + formatLength(p.cutWidth) +
                  (p.otherBands ? "  - other edges: other band" : ""),
              true);
    for (std::size_t i = 0; i < p.edges.size(); ++i)
        o += text(x + (i % 2 == 0 ? 0.0 : 70.0), top + 10 + 4.5 * static_cast<double>(i / 2), 2.9,
                  edgeText(p.edges[i], p.stepsTotal));
    return o + line(kMargin, top + kRowH, kPageW - kMargin, top + kRowH, 0.1);
}

std::string contourRow(const ContourBandingJob& c, const domain::EdgeBandId& band, double top) {
    std::string o = text(kMargin, top + 5, 3.4,
                         "#" + std::to_string(c.partNumber) + " " + c.partName + " (" + c.partId.str() +
                             ")  blank " + formatLength(c.cutLength) + " x " + formatLength(c.cutWidth) + "  - " +
                             (c.routed ? "SHAPED" : "per-segment bands") + ": " + kContourBandingNote,
                         true);
    std::vector<std::string> items;
    for (const auto& s : c.strips)
        if (s.edgeBandId == band)
            items.push_back(formatLength(s.length) + " (strip " + formatLength(s.stripLength) + ")");
    if (!c.lengthsAvailable) items.push_back("lengths unavailable - outline invalid, see diagnostics");
    for (std::size_t lineNo = 0; lineNo < 2 && lineNo * kStripsPerLine < items.size(); ++lineNo) {
        std::string s = lineNo == 0 ? "strips: " : "";
        const std::size_t end = std::min(items.size(), (lineNo + 1) * kStripsPerLine);
        for (std::size_t i = lineNo * kStripsPerLine; i < end; ++i) s += (i % kStripsPerLine ? "; " : "") + items[i];
        if (lineNo == 1 && items.size() > end)
            s += "; +" + std::to_string(items.size() - end) + " more (see text / CSV)";
        o += text(kMargin + 4, top + 10 + 4.5 * static_cast<double>(lineNo), 2.9, s);
    }
    return o + line(kMargin, top + kRowH, kPageW - kMargin, top + kRowH, 0.1);
}

std::string page(const std::string& header, const std::vector<std::string>& rows, std::size_t index,
                 std::size_t count) {
    std::string o = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"210mm\" height=\"297mm\" viewBox=\"0 0 210 297\""
                    " font-family=\"sans-serif\">\n<rect width=\"210\" height=\"297\" fill=\"#fff\"/>\n";
    o += header;
    for (std::size_t i = 0; i < rows.size(); ++i) o += rows[i];
    o += text(kPageW - kMargin, kPageH - kMargin / 2, 2.8,
              "page " + std::to_string(index + 1) + " / " + std::to_string(count), false, "end");
    return o + "</svg>\n";
}

} // namespace

std::vector<EdgeBandJobSheet> edgeBandingJobSheetsSvg(const EdgeBandingJobs& jobs) {
    // Band order: groups order, then bands used only by shaped parts (contourTotals order).
    std::vector<domain::EdgeBandId> bands;
    std::set<domain::EdgeBandId> seen;
    for (const auto& g : jobs.groups)
        if (seen.insert(g.edgeBandId).second) bands.push_back(g.edgeBandId);
    for (const auto& t : jobs.contourTotals)
        if (seen.insert(t.edgeBandId).second) bands.push_back(t.edgeBandId);

    const auto rowsPerPage = static_cast<std::size_t>((kPageH - kRowsTop - kFooter) / kRowH);
    std::vector<EdgeBandJobSheet> out;
    for (const auto& band : bands) {
        const EdgeBandJobGroup* g = nullptr;
        const ContourBandTotal* ct = nullptr;
        for (const auto& x : jobs.groups)
            if (x.edgeBandId == band) g = &x;
        for (const auto& x : jobs.contourTotals)
            if (x.edgeBandId == band) ct = &x;
        const bool missing = g ? g->missing : ct->missing;
        const std::string name = g ? g->name : ct->name;

        std::string title = "Edge banding - " + band.str() + (missing ? " (MISSING from the model)" : " '" + name + "'");
        if (g && !g->missing) title += "  " + formatLength(g->thickness) + " x " + formatLength(g->width) + " mm";
        std::string totals = "Overhang " + formatLength(jobs.overhangPerEnd) + " mm per strip end.";
        if (g)
            totals += "  Straight: " + std::to_string(g->parts.size()) + " part(s), " + std::to_string(g->edgeCount) +
                      " edge(s), " + formatLength(g->totalEdgeLength / 1000, 3) + " m, incl. overhang " +
                      formatLength(g->totalStripLength / 1000, 3) + " m.";
        if (ct)
            totals += "  Contour: " + std::to_string(ct->stripCount) + " strip(s), " +
                      formatLength(ct->totalLength / 1000, 3) + " m, incl. overhang " +
                      formatLength(ct->totalStripLength / 1000, 3) + " m.";
        const std::string note = kBandingOrderNote;
        const auto cutAt = note.find(", so ");
        std::string header = text(kMargin, 18, 5.5, title, true) + text(kMargin, 25, 3.2, totals) +
                             text(kMargin, 31, 2.8, note.substr(0, cutAt + 1)) +
                             text(kMargin, 35, 2.8, cutAt == std::string::npos ? "" : note.substr(cutAt + 2)) +
                             line(kMargin, 38, kPageW - kMargin, 38, 0.3);

        std::vector<std::vector<std::string>> pages(1);
        auto add = [&](auto&& render) {
            if (pages.back().size() == rowsPerPage) pages.emplace_back();
            const double top = kRowsTop + kRowH * static_cast<double>(pages.back().size());
            pages.back().push_back(render(top));
        };
        if (g)
            for (const auto& p : g->parts) add([&](double top) { return partRow(p, top); });
        for (const auto& c : jobs.contour) {
            bool uses = false;
            for (const auto& s : c.segments) uses = uses || s.edgeBandId == band;
            if (uses) add([&](double top) { return contourRow(c, band, top); });
        }
        EdgeBandJobSheet sheet;
        sheet.edgeBandId = band;
        for (std::size_t i = 0; i < pages.size(); ++i) sheet.pages.push_back(page(header, pages[i], i, pages.size()));
        out.push_back(std::move(sheet));
    }
    return out;
}

} // namespace fos::production
