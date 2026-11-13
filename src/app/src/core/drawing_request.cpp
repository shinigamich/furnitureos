// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/drawing_request.hpp"

#include "fos/drawings/dxf.hpp"
#include "fos/drawings/svg.hpp"
#include "fos/printing/pdf.hpp"
#include "fos/production/csv.hpp"

namespace fos::app {

namespace fs = std::filesystem;

namespace {
std::string s(const QString& text) { return text.toStdString(); }

fs::path numbered(const fs::path& path, std::size_t n) {
    fs::path out = path;
    out.replace_filename(path.stem().string() + "-" + std::to_string(n) + path.extension().string());
    return out;
}
} // namespace

drawings::TitleBlockLabels DrawingText::titleLabels() {
    drawings::TitleBlockLabels l;
    l.project = s(tr("Project"));
    l.title = s(tr("Title"));
    l.author = s(tr("Author"));
    l.date = s(tr("Date"));
    l.scale = s(tr("Scale"));
    l.sheet = s(tr("Sheet"));
    return l;
}

drawings::CalibrationOptions DrawingText::calibration() {
    drawings::CalibrationOptions c;
    c.unit = s(tr("mm"));
    c.noteLines = {s(tr("Print at 100 % (\"actual size\"), without \"fit to page\".")),
                   s(tr("Check with a ruler: the mark above must measure exactly the size stated."))};
    return c;
}

core::Result<drawings::DrawingResult> generateDrawing(const project::Project& project, const project::Resolved& resolved,
                                                      const DrawingRequest& request) {
    drawings::SheetOptions sheet;
    sheet.format = request.format;
    sheet.orientation = request.orientation;
    sheet.labels = DrawingText::titleLabels();
    sheet.calibration = DrawingText::calibration();
    sheet.title.project = project.metadata.name;
    sheet.title.author = project.metadata.author;
    sheet.title.date = request.date;
    sheet.scale = request.scale ? drawings::ScaleRequest::explicitScale(request.scale->numerator, request.scale->denominator)
                                : drawings::ScaleRequest::fit();
    if (request.kind == DrawingRequest::Kind::Assembly) {
        drawings::AssemblyDrawingOptions options;
        options.sheet = sheet; // empty title -> the assembly name
        return project::assemblyDrawing(project, resolved, request.id, options);
    }
    drawings::PartDrawingOptions options;
    options.sheet = sheet; // empty title -> the part name
    return project::partDrawing(project, resolved, request.id, options);
}

core::Result<WrittenFiles> exportDrawingSvg(const drawings::DrawingResult& drawing, const fs::path& path) {
    if (drawing.sheets.empty())
        return core::makeError(core::ErrorCode::InvalidArgument, "The drawing has no sheet to export.");
    drawings::SvgOptions options;
    options.background = "#ffffff";
    WrittenFiles written;
    for (std::size_t i = 0; i < drawing.sheets.size(); ++i) {
        const fs::path file = drawing.sheets.size() == 1 ? path : numbered(path, i + 1);
        if (auto r = production::writeTextFile(file, drawings::toSvg(drawing.sheets[i], options)); !r) return r.error();
        written.push_back(file);
    }
    return written;
}

core::Result<WrittenFiles> exportDrawingDxf(const drawings::DrawingResult& drawing, const fs::path& path) {
    if (drawing.views.empty())
        return core::makeError(core::ErrorCode::InvalidArgument, "The drawing has no view to export.");
    WrittenFiles written;
    for (std::size_t i = 0; i < drawing.views.size(); ++i) {
        const auto& v = drawing.views[i];
        double annotationScale = 1.0;
        if (v.sheetIndex < drawing.sheets.size()) {
            const double f = drawing.sheets[v.sheetIndex].scale.factor();
            if (f > 0) annotationScale = 1.0 / f;
        }
        const fs::path file = drawing.views.size() == 1 ? path : numbered(path, i + 1);
        const std::string text = drawings::viewToDxf(v.view, v.annotations, {}, annotationScale);
        if (auto r = production::writeTextFile(file, text); !r) return r.error();
        written.push_back(file);
    }
    return written;
}

core::Result<void> exportDrawingPdf(const drawings::DrawingResult& drawing, const fs::path& path,
                                    const std::string& title) {
    printing::PdfOptions options;
    options.title = title;
    return printing::exportPdf(drawing.sheets, path, options);
}

} // namespace fos::app
