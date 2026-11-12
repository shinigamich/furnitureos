// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/printing/print_plan.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>

namespace fos::printing {

namespace {

using core::Diagnostic;
using core::Severity;
using drawings::PaperOrientation;

constexpr double kEps = 1e-6;         // mm; sizes equal within this are "the same"
constexpr double kInkHalfWeight = 0.35; // mm; half of the heaviest standard line (0.7)

/// Number with at most `decimals` decimals, trailing zeros removed ("210", "0.5").
std::string num(double v, int decimals = 2) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}
std::string size(double w, double h) { return num(w) + " x " + num(h) + " mm"; }

std::string replaceAll(std::string s, const std::string& key, const std::string& value) {
    for (std::size_t pos = s.find(key); pos != std::string::npos; pos = s.find(key, pos + value.size()))
        s.replace(pos, key.size(), value);
    return s;
}

Diagnostic warn(std::string code, std::string message, const drawings::Sheet& sheet) {
    return {Severity::Warning, std::move(code), std::move(message), sheet.id};
}
Diagnostic info(std::string code, std::string message, const drawings::Sheet& sheet) {
    return {Severity::Info, std::move(code), std::move(message), sheet.id};
}

/// Bounding box of everything drawn, in sheet mm from the TOP-left corner (Y down),
/// widened by half the heaviest line weight. nullopt for a sheet without primitives.
std::optional<RectMm> inkBox(const drawings::Sheet& sheet) {
    const drawings::Rect2 b = drawings::boundsOf(sheet.primitives);
    if (b.empty()) return std::nullopt;
    return RectMm{b.min.x - kInkHalfWeight, sheet.height - b.max.y - kInkHalfWeight, b.width() + 2 * kInkHalfWeight,
                  b.height() + 2 * kInkHalfWeight};
}

/// Bounding box of the calibration mark (line or rectangle), sheet mm from the top-left.
std::optional<RectMm> calibrationBox(const drawings::Sheet& sheet) {
    drawings::Rect2 b;
    for (const drawings::Primitive& p : sheet.primitives) {
        const std::string& id = drawings::elementIdOf(p);
        if (id == "sheet/calibration.line" || id == "sheet/calibration.rect") b.expand(drawings::boundsOf(p));
    }
    if (b.empty()) return std::nullopt;
    return RectMm{b.min.x, sheet.height - b.max.y, b.width(), b.height()};
}

RectMm intersect(const RectMm& a, const RectMm& b) {
    const double x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
    const double x1 = std::min(a.right(), b.right()), y1 = std::min(a.bottom(), b.bottom());
    return {x0, y0, std::max(0.0, x1 - x0), std::max(0.0, y1 - y0)};
}
bool contains(const RectMm& outer, const RectMm& inner) {
    return inner.x >= outer.x - kEps && inner.y >= outer.y - kEps && inner.right() <= outer.right() + kEps &&
           inner.bottom() <= outer.bottom() + kEps;
}

std::optional<core::Error> validate(const drawings::Sheet& sheet, const PrinterPageInfo& page,
                                    const PrintPlanOptions& o) {
    const auto bad = [&](std::string message) {
        return core::makeError(core::ErrorCode::InvalidArgument, std::move(message), sheet.id);
    };
    const auto finite = [](double v) { return std::isfinite(v); };
    if (!finite(sheet.width) || !finite(sheet.height) || sheet.width <= 0 || sheet.height <= 0)
        return bad("Sheet '" + sheet.id + "' has no valid paper size (" + size(sheet.width, sheet.height) + ").");
    if (!finite(page.paperWidth) || !finite(page.paperHeight) || page.paperWidth <= 0 || page.paperHeight <= 0)
        return bad("The printer paper size " + size(page.paperWidth, page.paperHeight) + " is not valid.");
    const RectMm& pr = page.printable;
    if (!finite(pr.x) || !finite(pr.y) || !finite(pr.width) || !finite(pr.height) || pr.width <= 0 || pr.height <= 0)
        return bad("The printable area of the printer page (" + size(pr.width, pr.height) + ") is not valid.");
    if (!contains(RectMm{0, 0, page.paperWidth, page.paperHeight}, pr))
        return bad("The printable area (" + size(pr.width, pr.height) + " at " + num(pr.x) + ", " + num(pr.y) +
                   ") lies outside the paper (" + size(page.paperWidth, page.paperHeight) + ").");
    if (!finite(o.tileOverlap) || o.tileOverlap < 0)
        return bad("Tile overlap must be >= 0 mm, got " + num(o.tileOverlap) + ".");
    if (!finite(o.tileMargin) || o.tileMargin < 0)
        return bad("Tile margin must be >= 0 mm, got " + num(o.tileMargin) + ".");
    if (!finite(o.stampStrip) || o.stampStrip < 0)
        return bad("Stamp strip height must be >= 0 mm, got " + num(o.stampStrip) + ".");
    if (!finite(o.labelHeight) || o.labelHeight <= 0)
        return bad("Label text height must be > 0 mm, got " + num(o.labelHeight) + ".");
    return std::nullopt;
}

PrintPlan basePlan(const PrinterPageInfo& page, bool isRotated, PrintMode mode) {
    PrintPlan plan;
    plan.mode = mode;
    plan.page = page;
    plan.paperRotated = isRotated;
    return plan;
}

/// ExactScale on a single page; nullopt when the content does not fit.
std::optional<PrintPlan> planSingleExact(const drawings::Sheet& sheet, const PrinterPageInfo& page, bool isRotated) {
    const RectMm& pr = page.printable;
    const std::optional<RectMm> ink = inkBox(sheet);
    if (ink && (ink->width > pr.width + kEps || ink->height > pr.height + kEps)) return std::nullopt;

    double ox = (page.paperWidth - sheet.width) / 2, oy = (page.paperHeight - sheet.height) / 2;
    double dx = 0, dy = 0;
    if (ink) {
        if (ox + ink->x < pr.x - kEps) dx = pr.x - (ox + ink->x);
        else if (ox + ink->right() > pr.right() + kEps) dx = pr.right() - (ox + ink->right());
        if (oy + ink->y < pr.y - kEps) dy = pr.y - (oy + ink->y);
        else if (oy + ink->bottom() > pr.bottom() + kEps) dy = pr.bottom() - (oy + ink->bottom());
    }
    ox += dx;
    oy += dy;

    PrintPlan plan = basePlan(page, isRotated, PrintMode::ExactScale);
    PrintPage pg;
    pg.offsetX = ox;
    pg.offsetY = oy;
    const RectMm sheetOnPaper{ox, oy, sheet.width, sheet.height};
    pg.clip = intersect(sheetOnPaper, pr);
    pg.sheetRegion = {pg.clip.x - ox, pg.clip.y - oy, pg.clip.width, pg.clip.height};
    plan.pages.push_back(std::move(pg));

    if (!contains(pr, sheetOnPaper))
        plan.warnings.push_back(
            warn("printing.printable_smaller",
                 "The printable area of the paper (" + size(pr.width, pr.height) + ") is smaller than sheet '" +
                     sheet.id + "' (" + size(sheet.width, sheet.height) +
                     "); all drawn content lies inside the printable area, only blank sheet border is cut off.",
                 sheet));
    if (std::fabs(dx) > kEps || std::fabs(dy) > kEps)
        plan.warnings.push_back(warn("printing.shifted",
                                     "Sheet '" + sheet.id + "' is shifted by " + num(dx) + " mm horizontally and " +
                                         num(dy) +
                                         " mm vertically from the paper centre so that all drawn content lies "
                                         "inside the printable area. The scale is not affected.",
                                     sheet));
    return plan;
}

int tileCount(double sheetSize, double content, double overlap) {
    if (sheetSize <= content + kEps) return 1;
    return static_cast<int>(std::ceil((sheetSize - overlap) / (content - overlap) - 1e-9));
}

core::Result<PrintPlan> planTiled(const drawings::Sheet& sheet, const PrinterPageInfo& page, bool isRotated,
                                  const PrintPlanOptions& o) {
    const RectMm& pr = page.printable;
    const double m = o.tileMargin;
    const double cw = pr.width - 2 * m, ch = pr.height - 2 * m;
    if (cw <= o.tileOverlap + 1.0 || ch <= o.tileOverlap + 1.0)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Tiling is impossible: the printable area " + size(pr.width, pr.height) + " minus " +
                                   num(m) + " mm tile margin leaves " + size(cw, ch) +
                                   " per page, which must exceed the overlap of " + num(o.tileOverlap) +
                                   " mm by at least 1 mm.",
                               sheet.id);
    const double stepX = cw - o.tileOverlap, stepY = ch - o.tileOverlap;

    PrintPlan plan = basePlan(page, isRotated, PrintMode::ExactScale);
    plan.tiled = true;
    plan.cols = tileCount(sheet.width, cw, o.tileOverlap);
    plan.rows = tileCount(sheet.height, ch, o.tileOverlap);
    const double originX = pr.x + m, originY = pr.y + m;
    const bool marks = m >= 1.0;
    const double markStart = 0.1 * m, markEnd = 0.9 * m;
    const double textHeight = std::min(o.labelHeight, 0.7 * m);

    for (int r = 0; r < plan.rows; ++r) {
        for (int c = 0; c < plan.cols; ++c) {
            PrintPage pg;
            pg.row = r + 1;
            pg.col = c + 1;
            const double x0 = c * stepX, y0 = r * stepY;
            const double w = std::min(cw, sheet.width - x0), h = std::min(ch, sheet.height - y0);
            pg.sheetRegion = {x0, y0, w, h};
            pg.clip = {originX, originY, w, h};
            pg.offsetX = originX - x0;
            pg.offsetY = originY - y0;
            if (marks) {
                const double L = pg.clip.x, R = pg.clip.right(), T = pg.clip.y, B = pg.clip.bottom();
                // Crop marks: two strokes per corner, in the margin, pointing away from the tile.
                for (const double x : {L, R}) {
                    pg.marks.push_back({x, T - markStart, x, T - markEnd});
                    pg.marks.push_back({x, B + markStart, x, B + markEnd});
                }
                for (const double y : {T, B}) {
                    pg.marks.push_back({L - markStart, y, L - markEnd, y});
                    pg.marks.push_back({R + markStart, y, R + markEnd, y});
                }
                // Registration marks on the join lines: where the next tile's content
                // starts (step) and where the previous tile's content ends (overlap).
                std::vector<double> xs, ys;
                if (c + 1 < plan.cols) xs.push_back(L + stepX);
                if (c > 0 && o.tileOverlap > kEps) xs.push_back(L + o.tileOverlap);
                if (r + 1 < plan.rows) ys.push_back(T + stepY);
                if (r > 0 && o.tileOverlap > kEps) ys.push_back(T + o.tileOverlap);
                for (const double x : xs) {
                    if (x > R - kEps) continue;
                    pg.marks.push_back({x, T - markStart, x, T - markEnd});
                    pg.marks.push_back({x, B + markStart, x, B + markEnd});
                }
                for (const double y : ys) {
                    if (y > B - kEps) continue;
                    pg.marks.push_back({L - markStart, y, L - markEnd, y});
                    pg.marks.push_back({R + markStart, y, R + markEnd, y});
                }
            }
            std::string label = o.tileLabel;
            label = replaceAll(label, "{rows}", std::to_string(plan.rows));
            label = replaceAll(label, "{cols}", std::to_string(plan.cols));
            label = replaceAll(label, "{row}", std::to_string(pg.row));
            label = replaceAll(label, "{col}", std::to_string(pg.col));
            pg.label = label;
            if (textHeight >= 1.0)
                pg.texts.push_back({pg.clip.x + 2.0, pg.clip.bottom() + 0.15 * m + 0.7 * textHeight, textHeight, label});
            plan.pages.push_back(std::move(pg));
        }
    }

    plan.warnings.push_back(info("printing.tiled",
                                 "Sheet '" + sheet.id + "' (" + size(sheet.width, sheet.height) +
                                     ") does not fit the printable area " + size(pr.width, pr.height) +
                                     " and is split at exact scale into " + std::to_string(plan.pages.size()) +
                                     " pages (" + std::to_string(plan.rows) + " rows x " + std::to_string(plan.cols) +
                                     " columns) of up to " + size(cw, ch) + " with " + num(o.tileOverlap) +
                                     " mm overlap. Join the pages on the registration marks.",
                                 sheet));
    if (!marks)
        plan.warnings.push_back(warn("printing.tiling.no_marks",
                                     "The tile margin of " + num(m) +
                                         " mm is too small for crop and registration marks; none are printed.",
                                     sheet));
    if (textHeight < 1.0)
        plan.warnings.push_back(warn("printing.tiling.no_labels",
                                     "The tile margin of " + num(m) +
                                         " mm is too small for page labels; none are printed.",
                                     sheet));
    if (const std::optional<RectMm> cal = calibrationBox(sheet)) {
        bool whole = false;
        for (const PrintPage& pg : plan.pages) whole = whole || contains(pg.sheetRegion, *cal);
        if (!whole)
            plan.warnings.push_back(warn("printing.calibration.split",
                                         "The calibration mark of sheet '" + sheet.id +
                                             "' is split across several pages; it can only be measured after the "
                                             "pages are joined.",
                                         sheet));
    }
    return plan;
}

struct FitResult {
    double factor = 1.0;
    PrintPlan plan;
};

core::Result<FitResult> planFit(const drawings::Sheet& sheet, const PrinterPageInfo& page, bool isRotated,
                                const PrintPlanOptions& o) {
    const RectMm& pr = page.printable;
    const double availH = pr.height - o.stampStrip;
    if (availH < 1.0)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "The stamp strip of " + num(o.stampStrip) + " mm leaves no room in the printable area " +
                                   size(pr.width, pr.height) + ".",
                               sheet.id);
    double s = std::min(pr.width / sheet.width, availH / sheet.height);
    if (!o.allowEnlarge) s = std::min(s, 1.0);
    if (std::fabs(s - 1.0) < 1e-9) s = 1.0;

    FitResult fit;
    fit.factor = s;
    fit.plan = basePlan(page, isRotated, PrintMode::FitToPage);
    fit.plan.scaleFactor = s;
    PrintPage pg;
    pg.scale = s;
    pg.offsetX = pr.x + (pr.width - s * sheet.width) / 2;
    pg.offsetY = pr.y + (availH - s * sheet.height) / 2;
    pg.clip = {pg.offsetX, pg.offsetY, s * sheet.width, s * sheet.height};
    pg.sheetRegion = {0, 0, sheet.width, sheet.height};
    if (s != 1.0) {
        const std::string stamp = formatScaleStamp(o.scaledLabel, s);
        if (o.stampStrip > kEps) {
            const double th = std::min(o.labelHeight, 0.8 * o.stampStrip);
            pg.texts.push_back({pr.x + 1.0, pr.bottom() - o.stampStrip / 2 + 0.35 * th, th, stamp});
        } else {
            pg.texts.push_back({pr.x + 1.0, pr.bottom() - 0.3 * o.labelHeight - 0.5, o.labelHeight, stamp});
        }
        fit.plan.warnings.push_back(
            warn("printing.scaled",
                 "Sheet '" + sheet.id + "' (" + size(sheet.width, sheet.height) + ") is printed at " +
                     num(s * 100.0, 1) + " % of its size (factor " + num(s, 4) + ") to fit the printable area " +
                     size(pr.width, pr.height) +
                     ". The printout is NOT to scale: do not measure from it. The page is stamped accordingly.",
                 sheet));
    }
    fit.plan.pages.push_back(std::move(pg));
    return fit;
}

void addCommonWarnings(PrintPlan& plan, const drawings::Sheet& sheet) {
    if (plan.paperRotated)
        plan.warnings.push_back(info("printing.paper_rotated",
                                     std::string("The paper is used in ") +
                                         (plan.page.orientation == PaperOrientation::Landscape ? "landscape" : "portrait") +
                                         " orientation (" + size(plan.page.paperWidth, plan.page.paperHeight) +
                                         ") because sheet '" + sheet.id + "' fits better that way.",
                                     sheet));
    plan.calibrationMarkPresent = calibrationBox(sheet).has_value();
    if (plan.calibrationMarkPresent) {
        plan.calibrationPrintedLength = 100.0 * plan.scaleFactor;
        plan.warnings.push_back(
            info("printing.calibration.present",
                 plan.scaleFactor == 1.0
                     ? "After printing, measure the calibration mark with a ruler: it must be exactly 100 mm long. "
                       "If it is not, the printer driver has scaled the page."
                     : "The 100 mm calibration mark will measure " + num(plan.calibrationPrintedLength, 1) +
                           " mm on this scaled printout.",
                 sheet));
    } else {
        plan.warnings.push_back(warn("printing.calibration.absent",
                                     "Sheet '" + sheet.id +
                                         "' has no calibration mark, so the printed scale cannot be checked with a "
                                         "ruler. Enable the calibration mark in the sheet options.",
                                     sheet));
    }
    plan.warnings.push_back(
        warn("printing.driver_scaling",
             "The application cannot control scaling done by the printer driver. In the printer dialog choose "
             "\"actual size\" / 100 % and disable \"fit to page\", \"shrink to printable area\" and borderless "
             "enlargement, otherwise the printout is not to scale.",
             sheet));
}

} // namespace

PrinterPageInfo PrinterPageInfo::withUniformMargin(double width, double height, double margin) {
    PrinterPageInfo p;
    p.paperWidth = width;
    p.paperHeight = height;
    p.printable = {margin, margin, width - 2 * margin, height - 2 * margin};
    p.orientation = width > height ? PaperOrientation::Landscape : PaperOrientation::Portrait;
    return p;
}

PrinterPageInfo rotated(const PrinterPageInfo& page) {
    const double left = page.printable.x, top = page.printable.y;
    const double right = page.paperWidth - page.printable.right();
    const double bottom = page.paperHeight - page.printable.bottom();
    PrinterPageInfo r;
    r.paperWidth = page.paperHeight;
    r.paperHeight = page.paperWidth;
    double nl, nt, nr, nb;
    if (page.orientation == PaperOrientation::Portrait) { // portrait top edge -> landscape left edge
        nl = top, nt = right, nr = bottom, nb = left;
        r.orientation = PaperOrientation::Landscape;
    } else { // and back
        nl = bottom, nt = left, nr = top, nb = right;
        r.orientation = PaperOrientation::Portrait;
    }
    r.printable = {nl, nt, r.paperWidth - nl - nr, r.paperHeight - nt - nb};
    return r;
}

std::string formatScaleStamp(const std::string& labelTemplate, double scaleFactor) {
    return replaceAll(labelTemplate, "{percent}", [&] {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.1f", scaleFactor * 100.0);
        return std::string(buf);
    }());
}

core::Result<PrintPlan> planPrint(const drawings::Sheet& sheet, const PrinterPageInfo& page, PrintMode mode,
                                  const PrintPlanOptions& options) {
    if (const std::optional<core::Error> e = validate(sheet, page, options)) return *e;

    // Candidate 0 is the paper as given; candidate 1 (optional) is turned by 90 degrees.
    std::vector<PrinterPageInfo> candidates{page};
    if (options.allowRotatePaper && std::fabs(page.paperWidth - page.paperHeight) > kEps)
        candidates.push_back(rotated(page));

    std::optional<PrintPlan> chosen;
    if (mode == PrintMode::FitToPage) {
        double best = 0;
        std::optional<core::Error> firstError;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            core::Result<FitResult> fit = planFit(sheet, candidates[i], i == 1, options);
            if (!fit) {
                if (!firstError) firstError = fit.error();
                continue;
            }
            if (!chosen || fit.value().factor > best + 1e-9) {
                best = fit.value().factor;
                chosen = std::move(fit.value().plan);
            }
        }
        if (!chosen) return *firstError;
    } else {
        for (std::size_t i = 0; i < candidates.size() && !chosen; ++i)
            chosen = planSingleExact(sheet, candidates[i], i == 1);
        if (!chosen && !options.allowTiling) {
            const RectMm ink = inkBox(sheet).value_or(RectMm{});
            return core::makeError(
                core::ErrorCode::ConstraintViolation,
                "Sheet '" + sheet.id + "' (" + size(sheet.width, sheet.height) + ", drawn content " +
                    size(ink.width, ink.height) + ") does not fit the printable area " +
                    size(page.printable.width, page.printable.height) + " of the " +
                    size(page.paperWidth, page.paperHeight) +
                    " paper at exact scale. Use larger paper, enable tiling over several pages, or print with "
                    "fit-to-page (not to scale).",
                sheet.id);
        }
        if (!chosen) {
            std::optional<core::Error> firstError;
            for (std::size_t i = 0; i < candidates.size(); ++i) {
                core::Result<PrintPlan> tiled = planTiled(sheet, candidates[i], i == 1, options);
                if (!tiled) {
                    if (!firstError) firstError = tiled.error();
                    continue;
                }
                if (!chosen || tiled.value().pages.size() < chosen->pages.size()) chosen = std::move(tiled.value());
            }
            if (!chosen) return *firstError;
        }
    }
    addCommonWarnings(*chosen, sheet);
    return std::move(*chosen);
}

} // namespace fos::printing
