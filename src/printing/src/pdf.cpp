// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/printing/pdf.hpp"

#include <QFile>
#include <QGuiApplication>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>

#include <algorithm>
#include <cmath>

namespace fos::printing {

namespace {

QString toQString(const std::string& utf8) {
    return QString::fromUtf8(utf8.data(), static_cast<qsizetype>(utf8.size()));
}

/// Page layout of exactly the sheet size, zero margins, full-page mode.
QPageLayout layoutFor(const drawings::Sheet& sheet) {
    // QPageSize is always stored "portrait"; the orientation is carried by the layout.
    const double shortSide = std::min(sheet.width, sheet.height), longSide = std::max(sheet.width, sheet.height);
    const QPageSize size(QSizeF(shortSide, longSide), QPageSize::Millimeter, toQString(sheet.id),
                         QPageSize::ExactMatch);
    QPageLayout layout(size, sheet.width > sheet.height ? QPageLayout::Landscape : QPageLayout::Portrait,
                       QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter, QMarginsF(0, 0, 0, 0));
    layout.setMode(QPageLayout::FullPageMode);
    return layout;
}

} // namespace

core::Result<void> exportPdf(const std::vector<drawings::Sheet>& sheets, const std::filesystem::path& file,
                             const PdfOptions& options) {
    using core::ErrorCode;
    const std::string fileName = file.string();
    if (sheets.empty())
        return core::makeError(ErrorCode::InvalidArgument, "No sheets were given for PDF export.", fileName);
    for (const drawings::Sheet& s : sheets)
        if (!std::isfinite(s.width) || !std::isfinite(s.height) || s.width <= 0 || s.height <= 0)
            return core::makeError(ErrorCode::InvalidArgument,
                                   "Sheet '" + s.id + "' has no valid paper size (" + std::to_string(s.width) + " x " +
                                       std::to_string(s.height) + " mm) and cannot be exported to PDF.",
                                   s.id);
    if (options.resolutionDpi < 72 || options.resolutionDpi > 9600)
        return core::makeError(ErrorCode::InvalidArgument,
                               "PDF resolution must be between 72 and 9600 dpi, got " +
                                   std::to_string(options.resolutionDpi) + ".",
                               fileName);
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) == nullptr)
        return core::makeError(ErrorCode::Internal,
                               "PDF export needs a running QGuiApplication (fonts are unavailable without it).",
                               fileName);

    QFile out(QString::fromStdWString(file.wstring()));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return core::makeError(ErrorCode::IoError,
                               "Cannot write PDF file '" + fileName + "': " + out.errorString().toStdString() + ".",
                               fileName);
    const auto fail = [&](const std::string& what) {
        out.close();
        out.remove(); // do not leave a truncated PDF behind
        return core::makeError(ErrorCode::IoError, "PDF export to '" + fileName + "' failed: " + what, fileName);
    };

    {
        QPdfWriter writer(&out);
        writer.setResolution(options.resolutionDpi);
        writer.setTitle(toQString(options.title.empty() ? sheets.front().id : options.title));
        writer.setCreator(toQString(options.creator));

        PaintOptions paint = options.paint;
        paint.unitsPerMm = 0.0; // from the writer's resolution

        QPainter painter;
        for (std::size_t i = 0; i < sheets.size(); ++i) {
            if (!writer.setPageLayout(layoutFor(sheets[i])))
                return fail("the page size " + std::to_string(sheets[i].width) + " x " +
                            std::to_string(sheets[i].height) + " mm of sheet '" + sheets[i].id + "' was rejected.");
            if (i == 0) {
                if (!painter.begin(&writer)) return fail("the PDF writer could not be started.");
            } else if (!writer.newPage()) {
                painter.end();
                return fail("page " + std::to_string(i + 1) + " could not be started.");
            }
            paintSheet(painter, sheets[i], paint);
        }
        if (!painter.end()) return fail("the document could not be finished.");
    }
    if (!out.flush() || out.error() != QFileDevice::NoError) return fail(out.errorString().toStdString() + ".");
    out.close();
    return {};
}

} // namespace fos::printing
