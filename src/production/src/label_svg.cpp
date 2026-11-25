// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/core/units.hpp"
#include "fos/production/code128.hpp"
#include "fos/production/html.hpp"
#include "fos/production/labels.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace fos::production {
namespace {

std::string n(double v) { return core::formatLength(v, 3); }

bool positive(double v) { return std::isfinite(v) && v > 0.0; }
bool nonNegative(double v) { return std::isfinite(v) && v >= 0.0; }

/// How many cells of size `cell` with `gap` between them fit into `available`.
int fit(double available, double cell, double gap) {
    const double k = std::floor((available + gap + 1e-9) / (cell + gap));
    return k < 1.0 ? 0 : (k > 10000.0 ? 10000 : static_cast<int>(k));
}

void text(std::string& out, double x, double y, double size, const std::string& content, const char* extra = "") {
    out += "<text x=\"" + n(x) + "\" y=\"" + n(y) + "\" font-size=\"" + n(size) + "\"" + extra + ">" +
           escapeXml(content) + "</text>\n";
}

/// Small panel outline: L1 (y = 0 of the part frame) at the bottom, L2 at the top,
/// W1 on the left, W2 on the right. Banded = heavy line, raw = hairline,
/// missing edge band reference = heavy dashed line.
void edgeDiagram(std::string& out, const Label& l, double x, double y, double w, double h) {
    struct Side { double x1, y1, x2, y2; };
    const Side sides[4] = {{x, y + h, x + w, y + h}, {x, y, x + w, y}, {x, y, x, y + h}, {x + w, y, x + w, y + h}};
    for (std::size_t i = 0; i < 4; ++i) {
        const char code = i < l.edgeCode.size() ? l.edgeCode[i] : '0';
        std::string style = code == '0' ? "stroke-width=\"0.15\"" : "stroke-width=\"0.9\"";
        if (code == '?') style += " stroke-dasharray=\"1 0.6\"";
        out += "<line x1=\"" + n(sides[i].x1) + "\" y1=\"" + n(sides[i].y1) + "\" x2=\"" + n(sides[i].x2) +
               "\" y2=\"" + n(sides[i].y2) + "\" stroke=\"#000\" " + style + "/>\n";
    }
}

// Barcode band in the 37 mm design space (replaces the part-id text line at baseline 29.5).
constexpr double kPad = 2.0;
constexpr double kBarcodeTop = 26.0, kBarcodeHeight = 5.6;

/// A barcode that passed the size checks, ready to draw.
struct PlacedBarcode {
    std::string text;
    Code128Symbol symbol;
    double module = 0; ///< mm on paper.
};

/// Bars as filled rects in label-local paper mm (outside the design-space scale, so module
/// widths are exact).
void renderBarcode(std::string& out, const PlacedBarcode& b, const LabelSheetOptions& o) {
    const double s = o.labelHeight / 37.0;
    const double y0 = kBarcodeTop * s, h = kBarcodeHeight * s;
    out += "<g data-barcode=\"code128\" data-text=\"" + escapeXml(b.text) + "\">\n";
    int at = kCode128QuietZoneModules; // modules from the start of the band
    for (std::size_t i = 0; i < b.symbol.widths.size(); ++i) {
        const int w = b.symbol.widths[i];
        if (i % 2 == 0)
            out += "<rect x=\"" + n(kPad * s + at * b.module) + "\" y=\"" + n(y0) + "\" width=\"" +
                   n(w * b.module) + "\" height=\"" + n(h) + "\"/>\n";
        at += w;
    }
    out += "</g>\n";
}

void renderLabel(std::string& out, const Label& l, const LabelSheetOptions& o, double x, double y,
                 const std::string& clipId, const PlacedBarcode* barcode) {
    // The content is designed for a 37 mm high label and scaled uniformly with the height.
    const double s = o.labelHeight / 37.0;
    const double w = o.labelWidth / s; // design-space width
    const double pad = kPad;
    const int d = o.decimals;
    auto len = [d](double v) { return core::formatLength(v, d); };
    const std::string times = " \xC3\x97 "; // U+00D7 MULTIPLICATION SIGN

    out += "<g transform=\"translate(" + n(x) + " " + n(y) + ")\">\n";
    out += "<clipPath id=\"" + clipId + "\"><rect width=\"" + n(o.labelWidth) + "\" height=\"" +
           n(o.labelHeight) + "\"/></clipPath>\n";
    if (o.drawBorders)
        out += "<rect width=\"" + n(o.labelWidth) + "\" height=\"" + n(o.labelHeight) +
               "\" fill=\"none\" stroke=\"#999\" stroke-width=\"0.1\"/>\n";
    out += "<g clip-path=\"url(#" + clipId + ")\"><g transform=\"scale(" + n(s) + ")\">\n";

    std::string head = l.projectName;
    if (!head.empty() && !l.assemblyName.empty()) head += " \xC2\xB7 "; // U+00B7 MIDDLE DOT
    head += l.assemblyName;
    text(out, pad, 5.0, 2.6, head);
    text(out, pad, 10.5, 3.6, o.text.number + " " + std::to_string(l.partNumber) + "  " + l.partName,
         " font-weight=\"bold\"");
    text(out, pad, 16.0, 3.4, len(l.length) + times + len(l.width) + times + len(l.thickness));
    text(out, pad, 20.5, 2.8, o.text.cut + " " + len(l.cutLength) + times + len(l.cutWidth));
    text(out, pad, 25.0, 2.8, l.materialName);
    if (!barcode) text(out, pad, 29.5, 2.2, l.partId.str());
    const double dw = 13.0, dh = 8.0;
    if (l.shaped) {
        // No rectangle diagram: its four edges say nothing about a contoured outline.
        text(out, pad, 34.5, 2.6, o.text.shaped + " " + l.outlineEdgeCode);
        text(out, w - pad, 19.0, 3.0, o.text.shaped, " text-anchor=\"end\" font-weight=\"bold\"");
    } else {
        text(out, pad, 34.5, 2.6, l.edgeCode);
        edgeDiagram(out, l, w - pad - dw, 13.5, dw, dh);
    }
    text(out, w - pad, 34.5, 4.0, l.quantityText(), " text-anchor=\"end\" font-weight=\"bold\"");
    if (barcode) {
        out += "</g>\n";
        renderBarcode(out, *barcode, o);
        out += "</g>\n</g>\n";
    } else {
        out += "</g></g>\n</g>\n";
    }
}

/// Encodes and sizes the barcode of one label; nullopt plus a warning when it cannot be drawn legibly.
std::optional<PlacedBarcode> placeBarcode(const Label& l, const LabelSheetOptions& o, core::Diagnostics& diags) {
    PlacedBarcode b;
    b.text = labelBarcodeText(l, o.barcodeContent);
    const std::string who = "part '" + l.partId.str() + "' (No. " + std::to_string(l.partNumber) + ")";
    auto symbol = encodeCode128B(b.text);
    if (!symbol) {
        diags.push_back({core::Severity::Warning, "production.label.barcode_unencodable",
                         "The barcode of " + who + " was omitted: " + symbol.error().message, l.partId.str()});
        return std::nullopt;
    }
    b.symbol = std::move(symbol.value());
    const double s = o.labelHeight / 37.0;
    const double band = o.labelWidth - 2.0 * kPad * s;
    const int total = b.symbol.modules() + 2 * kCode128QuietZoneModules;
    b.module = band / total;
    if (!(b.module >= o.barcodeMinModule)) {
        diags.push_back({core::Severity::Warning, "production.label.barcode_too_narrow",
                         "The Code 128 barcode of " + who + " (\"" + b.text + "\") needs " + std::to_string(total) +
                             " modules including quiet zones; the " + n(band) + " mm barcode area of the " +
                             n(o.labelWidth) + " mm wide label allows only " + n(b.module) +
                             " mm per module, below the minimum of " + n(o.barcodeMinModule) +
                             " mm. The barcode was omitted; use wider labels or encode the part id only.",
                         l.partId.str()});
        return std::nullopt;
    }
    return b;
}

} // namespace

core::Result<LabelGrid> labelGrid(const LabelSheetOptions& o) {
    auto invalid = [](std::string msg) { return core::makeError(core::ErrorCode::InvalidArgument, std::move(msg)); };
    if (!positive(o.pageWidth) || !positive(o.pageHeight))
        return invalid("Label sheet page size " + n(o.pageWidth) + " x " + n(o.pageHeight) +
                       " mm must be positive.");
    if (!positive(o.labelWidth) || !positive(o.labelHeight))
        return invalid("Label size " + n(o.labelWidth) + " x " + n(o.labelHeight) + " mm must be positive.");
    if (!nonNegative(o.marginLeft) || !nonNegative(o.marginTop) || !nonNegative(o.gapX) || !nonNegative(o.gapY))
        return invalid("Label sheet margins (" + n(o.marginLeft) + ", " + n(o.marginTop) + " mm) and gaps (" +
                       n(o.gapX) + ", " + n(o.gapY) + " mm) must not be negative.");
    LabelGrid g;
    g.columns = fit(o.pageWidth - o.marginLeft, o.labelWidth, o.gapX);
    g.rows = fit(o.pageHeight - o.marginTop, o.labelHeight, o.gapY);
    if (g.columns < 1 || g.rows < 1)
        return invalid("A " + n(o.labelWidth) + " x " + n(o.labelHeight) + " mm label does not fit on a " +
                       n(o.pageWidth) + " x " + n(o.pageHeight) + " mm page with margins " + n(o.marginLeft) +
                       " (left) and " + n(o.marginTop) + " (top) mm.");
    return g;
}

std::string labelBarcodeText(const Label& l, LabelBarcodeContent content) {
    if (content == LabelBarcodeContent::ProjectAndPartId) return l.projectName + "|" + l.partId.str();
    return l.partId.str();
}

core::Result<std::vector<std::string>> renderLabelSheetsSvg(const std::vector<Label>& labels,
                                                            const LabelSheetOptions& o) {
    auto sheets = renderLabelSheets(labels, o);
    if (!sheets) return sheets.error();
    return std::move(sheets.value().pages);
}

core::Result<LabelSheets> renderLabelSheets(const std::vector<Label>& labels, const LabelSheetOptions& o) {
    const auto grid = labelGrid(o);
    if (!grid) return grid.error();
    if (o.barcode != LabelBarcode::None && !(positive(o.barcodeMinModule)))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Minimum barcode module width " + n(o.barcodeMinModule) + " mm must be positive.");
    LabelSheets result;
    const std::size_t columns = static_cast<std::size_t>(grid.value().columns);
    const std::size_t perPage = static_cast<std::size_t>(grid.value().perPage());

    std::vector<std::string>& pages = result.pages;
    for (std::size_t first = 0; first < labels.size(); first += perPage) {
        std::string svg = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"" + n(o.pageWidth) +
               "mm\" height=\"" + n(o.pageHeight) + "mm\" viewBox=\"0 0 " + n(o.pageWidth) + " " +
               n(o.pageHeight) + "\" font-family=\"DejaVu Sans, Arial, Helvetica, sans-serif\" fill=\"#000\">\n";
        const std::size_t last = std::min(labels.size(), first + perPage);
        for (std::size_t i = first; i < last; ++i) {
            const std::size_t slot = i - first;
            const double x = o.marginLeft + static_cast<double>(slot % columns) * (o.labelWidth + o.gapX);
            const double y = o.marginTop + static_cast<double>(slot / columns) * (o.labelHeight + o.gapY);
            std::optional<PlacedBarcode> barcode;
            if (o.barcode == LabelBarcode::Code128) barcode = placeBarcode(labels[i], o, result.diagnostics);
            renderLabel(svg, labels[i], o, x, y, "label-clip-" + std::to_string(slot + 1),
                        barcode ? &*barcode : nullptr);
        }
        svg += "</svg>\n";
        pages.push_back(std::move(svg));
    }
    return result;
}

} // namespace fos::production
