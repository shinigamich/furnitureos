// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/printing/print.hpp"

#include "paint_detail.hpp"

#include <QGuiApplication>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPrinter>

#include <algorithm>
#include <cmath>

namespace fos::printing {

namespace {

QString toQString(const std::string& utf8) {
    return QString::fromUtf8(utf8.data(), static_cast<qsizetype>(utf8.size()));
}

/// Paints a plan page in a painter whose units are already mm (origin = paper corner).
void paintPageMm(QPainter& painter, const drawings::Sheet& sheet, const PrintPage& page, const PaintOptions& options) {
    PaintOptions o = options;
    o.unitsPerMm = 1.0;
    painter.save();
    painter.setClipRect(QRectF(page.clip.x, page.clip.y, page.clip.width, page.clip.height), Qt::IntersectClip);
    painter.translate(page.offsetX, page.offsetY);
    painter.scale(page.scale, page.scale);
    paintSheet(painter, sheet, o);
    painter.restore();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, options.antialiasing);
    QPen pen(options.color);
    pen.setWidthF(0.2);
    pen.setCapStyle(Qt::FlatCap);
    painter.setPen(pen);
    for (const PlanMark& m : page.marks) painter.drawLine(QLineF(m.x1, m.y1, m.x2, m.y2));
    painter.restore();
    for (const PlanText& t : page.texts)
        detail::drawTextMm(painter, QPointF(t.x, t.y), t.height, -1, 0.0, toQString(t.text), options.fontFamily,
                           options.color);
}

struct Job {
    QPageLayout layout;
    PrintPlan plan;
};

} // namespace

PrinterPageInfo printerPageInfo(const QPrinter& printer) {
    const QPageLayout layout = printer.pageLayout();
    const QRectF full = layout.fullRect(QPageLayout::Millimeter);
    QPageLayout mm = layout;
    mm.setUnits(QPageLayout::Millimeter);
    const QMarginsF min = mm.minimumMargins();
    const double l = std::max(0.0, min.left()), t = std::max(0.0, min.top());
    const double r = std::max(0.0, min.right()), b = std::max(0.0, min.bottom());
    PrinterPageInfo info;
    info.paperWidth = full.width();
    info.paperHeight = full.height();
    info.printable = {l, t, full.width() - l - r, full.height() - t - b};
    info.orientation = layout.orientation() == QPageLayout::Landscape ? drawings::PaperOrientation::Landscape
                                                                      : drawings::PaperOrientation::Portrait;
    return info;
}

void paintPlanPage(QPainter& painter, const drawings::Sheet& sheet, const PrintPage& page,
                   const PaintOptions& options) {
    painter.save();
    detail::scaleToMm(painter, options.unitsPerMm);
    paintPageMm(painter, sheet, page, options);
    painter.restore();
}

QImage renderPlanPagePreview(const drawings::Sheet& sheet, const PrintPlan& plan, std::size_t pageIndex,
                             double pixelsPerMm, const PaintOptions& options) {
    if (pageIndex >= plan.pages.size() || !(pixelsPerMm > 0) || !std::isfinite(pixelsPerMm)) return {};
    const double w = std::ceil(plan.page.paperWidth * pixelsPerMm - 1e-9);
    const double h = std::ceil(plan.page.paperHeight * pixelsPerMm - 1e-9);
    if (!(w >= 1) || !(h >= 1) || w > kMaxPreviewSidePx || h > kMaxPreviewSidePx) return {};
    QImage image(static_cast<int>(w), static_cast<int>(h), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    image.fill(Qt::white);
    QPainter painter(&image);
    PaintOptions o = options;
    o.unitsPerMm = pixelsPerMm;
    paintPlanPage(painter, sheet, plan.pages[pageIndex], o);
    painter.end();
    return image;
}

core::Result<void> printSheets(QPrinter& printer, const std::vector<drawings::Sheet>& sheets,
                               const PrintOptions& options, std::vector<PrintPlan>* executedPlans) {
    using core::ErrorCode;
    if (sheets.empty()) return core::makeError(ErrorCode::InvalidArgument, "No sheets were given for printing.");
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) == nullptr)
        return core::makeError(ErrorCode::Internal,
                               "Printing needs a running QGuiApplication (fonts are unavailable without it).");

    printer.setFullPage(true);
    const QPageLayout original = printer.pageLayout();

    // Pass 1: configure the paper and plan every sheet; nothing is printed yet.
    std::vector<Job> jobs;
    for (const drawings::Sheet& sheet : sheets) {
        printer.setPageLayout(original);
        if (options.matchPaperToSheet && sheet.width > 0 && sheet.height > 0 && std::isfinite(sheet.width) &&
            std::isfinite(sheet.height)) {
            const double shortSide = std::min(sheet.width, sheet.height), longSide = std::max(sheet.width, sheet.height);
            const QPageSize size(QSizeF(shortSide, longSide), QPageSize::Millimeter, QString(), QPageSize::ExactMatch);
            const bool set = printer.setPageSize(size) &&
                             printer.setPageOrientation(sheet.width > sheet.height ? QPageLayout::Landscape
                                                                                  : QPageLayout::Portrait);
            const PrinterPageInfo got = printerPageInfo(printer);
            if (!set || std::fabs(got.paperWidth - sheet.width) > 0.5 || std::fabs(got.paperHeight - sheet.height) > 0.5)
                printer.setPageLayout(original); // not supported: keep the configured paper
        }
        core::Result<PrintPlan> plan = planPrint(sheet, printerPageInfo(printer), options.mode, options.plan);
        if (plan && plan.value().paperRotated) {
            // Turn the real paper and plan again against what the driver reports for it.
            const bool toLandscape = plan.value().page.orientation == drawings::PaperOrientation::Landscape;
            printer.setPageOrientation(toLandscape ? QPageLayout::Landscape : QPageLayout::Portrait);
            PrintPlanOptions fixed = options.plan;
            fixed.allowRotatePaper = false;
            plan = planPrint(sheet, printerPageInfo(printer), options.mode, fixed);
        }
        if (!plan) {
            printer.setPageLayout(original);
            return plan.error();
        }
        jobs.push_back({printer.pageLayout(), std::move(plan.value())});
    }

    // Pass 2: print.
    PaintOptions paint = options.paint;
    paint.unitsPerMm = 0.0; // from the printer resolution
    QPainter painter;
    bool first = true;
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        printer.setPageLayout(jobs[i].layout);
        for (const PrintPage& page : jobs[i].plan.pages) {
            if (first) {
                if (!painter.begin(&printer))
                    return core::makeError(ErrorCode::IoError,
                                           "The printer '" + printer.printerName().toStdString() +
                                               "' could not be started (is it available and is the output file "
                                               "writable?).",
                                           sheets[i].id);
                first = false;
            } else if (!printer.newPage()) {
                painter.end();
                return core::makeError(ErrorCode::IoError,
                                       "The printer refused to start a new page for sheet '" + sheets[i].id +
                                           "' (page " + std::to_string(page.row) + "/" + std::to_string(page.col) + ").",
                                       sheets[i].id);
            }
            paintPlanPage(painter, sheets[i], page, paint);
        }
    }
    const bool ended = painter.end();
    if (!ended || printer.printerState() == QPrinter::Error)
        return core::makeError(ErrorCode::IoError, "The printer reported an error; the print job may be incomplete.");
    if (executedPlans) {
        executedPlans->clear();
        for (Job& j : jobs) executedPlans->push_back(std::move(j.plan));
    }
    return {};
}

} // namespace fos::printing
