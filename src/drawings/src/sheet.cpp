// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/drawings/sheet.hpp"

#include <algorithm>
#include <cmath>

namespace fos::drawings {

core::Result<PaperSize> paperSize(PaperFormat format, PaperOrientation orientation, PaperSize custom) {
    PaperSize p;
    switch (format) {
    case PaperFormat::A4: p = {210, 297}; break;
    case PaperFormat::A3: p = {297, 420}; break;
    case PaperFormat::A2: p = {420, 594}; break;
    case PaperFormat::A1: p = {594, 841}; break;
    case PaperFormat::Custom:
        if (!(custom.width > 0 && custom.height > 0) || !std::isfinite(custom.width) || !std::isfinite(custom.height))
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Custom paper size " + core::formatLength(custom.width) + " x " +
                                       core::formatLength(custom.height) + " mm is not valid; both sides must be positive.");
        p = custom;
        break;
    }
    const bool portrait = orientation == PaperOrientation::Portrait;
    if ((portrait && p.width > p.height) || (!portrait && p.width < p.height)) std::swap(p.width, p.height);
    return p;
}

const char* toString(PaperFormat f) noexcept {
    switch (f) {
    case PaperFormat::A4: return "A4";
    case PaperFormat::A3: return "A3";
    case PaperFormat::A2: return "A2";
    case PaperFormat::A1: return "A1";
    case PaperFormat::Custom: return "custom";
    }
    return "?";
}

std::string Scale::text() const {
    return core::formatLength(numerator, 3) + ":" + core::formatLength(denominator, 3);
}

const std::vector<Scale>& standardScales() {
    static const std::vector<Scale> scales = {{1, 1}, {1, 2}, {1, 5}, {1, 10}, {1, 20}, {1, 50}, {1, 100}};
    return scales;
}

namespace {

Line line(Vec2 a, Vec2 b, LineStyle style, double weight, const char* element) {
    return Line{a, b, style, weight, {}, element};
}

Text text(Vec2 pos, double height, std::string value, std::string element, HAlign h = HAlign::Left,
          VAlign v = VAlign::Baseline) {
    Text t;
    t.position = pos;
    t.height = height;
    t.hAlign = h;
    t.vAlign = v;
    t.text = std::move(value);
    t.elementId = std::move(element);
    return t;
}

void addRect(std::vector<Primitive>& out, const Rect2& r, LineStyle style, double weight, const char* element) {
    Polyline p;
    p.points = {r.min, {r.max.x, r.min.y}, r.max, {r.min.x, r.max.y}};
    p.closed = true;
    p.style = style;
    p.weight = weight;
    p.elementId = element;
    out.emplace_back(std::move(p));
}

constexpr double kTitleWidth = 180.0, kTitleHeight = 28.0, kNoteHeight = 2.5, kNoteStep = 3.5, kPad = 4.0;

double calibrationHeight(const CalibrationOptions& c) {
    const double notes = static_cast<double>(c.noteLines.size()) * kNoteStep;
    switch (c.kind) {
    case CalibrationKind::None: return 0.0;
    case CalibrationKind::Line100: return notes + 2.0 + 3.5 + 1.0 + kNoteHeight; // ticks 3.5, gap, label
    case CalibrationKind::Rectangle10050: return notes + 2.0 + 50.0;
    }
    return 0.0;
}

/// Draws the calibration block with its bottom-left corner at `c`.
void addCalibration(std::vector<Primitive>& out, const CalibrationOptions& opt, Vec2 c) {
    const std::size_t n = opt.noteLines.size();
    for (std::size_t i = 0; i < n; ++i)
        out.emplace_back(text({c.x, c.y + static_cast<double>(n - 1 - i) * kNoteStep + 0.6}, kNoteHeight,
                              opt.noteLines[i], "sheet/calibration.note"));
    const double y = c.y + static_cast<double>(n) * kNoteStep + 2.0;
    const std::string unit = opt.unit.empty() ? std::string() : " " + opt.unit;
    if (opt.kind == CalibrationKind::Line100) {
        out.emplace_back(line({c.x, y}, {c.x + 100.0, y}, LineStyle::Visible, 0.35, "sheet/calibration.line"));
        for (int i = 0; i <= 10; ++i) {
            const double x = c.x + 10.0 * i;
            const double h = (i % 5 == 0) ? 3.5 : 2.0;
            out.emplace_back(line({x, y}, {x, y + h}, LineStyle::Thin, 0.18, "sheet/calibration.tick"));
        }
        out.emplace_back(text({c.x + 50.0, y + 4.5}, kNoteHeight, core::formatLength(100.0) + unit,
                              "sheet/calibration.label", HAlign::Center));
    } else if (opt.kind == CalibrationKind::Rectangle10050) {
        addRect(out, {{c.x, y}, {c.x + 100.0, y + 50.0}}, LineStyle::Visible, 0.35, "sheet/calibration.rect");
        out.emplace_back(text({c.x + 50.0, y + 25.0}, kNoteHeight,
                              core::formatLength(100.0) + " \xC3\x97 " + core::formatLength(50.0) + unit,
                              "sheet/calibration.label", HAlign::Center, VAlign::Middle));
    }
}

void addTitleBlock(std::vector<Primitive>& out, const SheetOptions& opt, const Scale& scale, const Rect2& tb) {
    const double f = tb.width() / kTitleWidth;
    const double x0 = tb.min.x, y0 = tb.min.y, yMid = y0 + kTitleHeight * 0.5, y1 = tb.max.y;
    addRect(out, tb, LineStyle::Visible, 0.5, "sheet/titleblock");
    const auto thin = [&](Vec2 a, Vec2 b) { out.emplace_back(line(a, b, LineStyle::Thin, 0.25, "sheet/titleblock")); };
    thin({x0, yMid}, {tb.max.x, yMid});
    thin({x0 + 90 * f, yMid}, {x0 + 90 * f, y1});
    for (const double x : {50.0, 90.0, 135.0}) thin({x0 + x * f, y0}, {x0 + x * f, yMid});
    const auto cell = [&](double x, double yBottom, double yTop, const std::string& label, const std::string& value,
                          const char* key, double valueHeight) {
        const std::string id = std::string("sheet/title.") + key;
        out.emplace_back(text({x0 + x * f + 1.5, yTop - 1.2}, 1.8, label, id + ".label", HAlign::Left, VAlign::Top));
        out.emplace_back(text({x0 + x * f + 1.5, yBottom + 3.0}, valueHeight, value, id));
    };
    const TitleBlockData& d = opt.title;
    cell(0, yMid, y1, opt.labels.project, d.project, "project", 4.0);
    cell(90, yMid, y1, opt.labels.title, d.title, "title", 4.0);
    cell(0, y0, yMid, opt.labels.author, d.author, "author", 3.5);
    cell(50, y0, yMid, opt.labels.date, d.date, "date", 3.5);
    cell(90, y0, yMid, opt.labels.scale, scale.text(), "scale", 3.5);
    cell(135, y0, yMid, opt.labels.sheet, std::to_string(d.sheetNumber) + "/" + std::to_string(d.sheetCount), "sheet",
         3.5);
}

} // namespace

core::Result<Sheet> makeSheet(const SheetOptions& opt, const Scale& scale, std::string id) {
    const auto paper = paperSize(opt.format, opt.orientation, opt.custom);
    if (!paper) return paper.error();
    if (!(scale.numerator > 0 && scale.denominator > 0) || !std::isfinite(scale.factor()))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Drawing scale " + scale.text() + " is not valid; both numbers must be positive.");
    const Margins& m = opt.margins;
    if (m.left < 0 || m.right < 0 || m.top < 0 || m.bottom < 0)
        return core::makeError(core::ErrorCode::InvalidArgument, "Sheet margins must not be negative.");
    Sheet sheet;
    sheet.id = std::move(id);
    sheet.width = paper.value().width;
    sheet.height = paper.value().height;
    sheet.scale = scale;
    sheet.frame = {{m.left, m.bottom}, {sheet.width - m.right, sheet.height - m.top}};

    const double tbWidth = std::min(kTitleWidth, sheet.frame.width());
    const double calH = calibrationHeight(opt.calibration);
    const bool hasCal = opt.calibration.kind != CalibrationKind::None;
    const bool beside = hasCal && sheet.frame.width() - tbWidth >= 115.0;
    double strip = kTitleHeight;
    if (hasCal) strip = beside ? std::max(kTitleHeight, calH + 2 * kPad) : kTitleHeight + calH + 2 * kPad;
    sheet.drawingArea = {{sheet.frame.min.x, sheet.frame.min.y + strip}, sheet.frame.max};
    if (sheet.frame.width() < 40.0 || sheet.drawingArea.height() < 40.0 ||
        (hasCal && sheet.frame.width() < 100.0 + 2 * kPad))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "A " + core::formatLength(sheet.width) + " x " + core::formatLength(sheet.height) +
                                   " mm sheet with these margins leaves no room for the title block" +
                                   (hasCal ? ", the 100 mm calibration mark" : "") +
                                   " and a drawing area of at least 40 x 40 mm.");

    addRect(sheet.primitives, sheet.frame, LineStyle::Border, defaultLineWeight(LineStyle::Border), "sheet/frame");
    const Rect2 tb{{sheet.frame.max.x - tbWidth, sheet.frame.min.y},
                   {sheet.frame.max.x, sheet.frame.min.y + kTitleHeight}};
    addTitleBlock(sheet.primitives, opt, scale, tb);
    if (hasCal) {
        const Vec2 corner = beside ? Vec2{sheet.frame.min.x + kPad, sheet.frame.min.y + kPad}
                                   : Vec2{sheet.frame.min.x + kPad, sheet.frame.min.y + kTitleHeight + kPad};
        addCalibration(sheet.primitives, opt.calibration, corner);
    }
    return sheet;
}

} // namespace fos::drawings
