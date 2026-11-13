// SPDX-License-Identifier: AGPL-3.0-only
// fos drawing
#include "common.hpp"

#include "fos/core/units.hpp"
#include "fos/drawings/dxf.hpp"
#include "fos/drawings/svg.hpp"
#include "fos/project/outputs.hpp"

namespace fos::cli::detail {
namespace {

const char* kDrawingHelp =
    "Usage: fos drawing <file.fosp> <item-id|part-id> (--svg out.svg | --dxf out.dxf)\n"
    "                   [--paper A4|A3|A2|A1] [--portrait] [--scale 1:N|N:M|fit] [--date TEXT]\n\n"
    "An item id gives the assembly drawing (front view, top view below, left view to the\n"
    "right - ISO first angle - with automatic dimensions); a part id (e.g. cab1/side.left)\n"
    "gives the part drawing (face A, holes, edge banding, finished and cut sizes). Stored\n"
    "drawing edits of the project are applied; stale text overrides are reported.\n"
    "--svg: the sheet at exact scale (width/height in mm, 1 user unit = 1 mm, 100 mm\n"
    "calibration line); further sheets get -2, -3, ... before the extension.\n"
    "--dxf: the first view (front view / face A) in model space at 1:1 in mm (DXF R12).\n"
    "Defaults: --paper A3, landscape, --scale fit (largest of 1:1 1:2 1:5 1:10 1:20 1:50 1:100\n"
    "that fits). An explicit scale that does not fit is an error. --date is printed in the\n"
    "title block (default: the date of the project's last modification).\n";

std::optional<drawings::PaperFormat> paperByName(const std::string& s) {
    if (s == "A4") return drawings::PaperFormat::A4;
    if (s == "A3") return drawings::PaperFormat::A3;
    if (s == "A2") return drawings::PaperFormat::A2;
    if (s == "A1") return drawings::PaperFormat::A1;
    return std::nullopt;
}

std::optional<drawings::ScaleRequest> scaleByText(const std::string& s) {
    if (s == "fit") return drawings::ScaleRequest::fit();
    const auto colon = s.find(':');
    if (colon == std::string::npos) return std::nullopt;
    const auto n = parseNumber(std::string_view(s).substr(0, colon));
    const auto d = parseNumber(std::string_view(s).substr(colon + 1));
    if (!n || !d || *n <= 0 || *d <= 0) return std::nullopt;
    return drawings::ScaleRequest::explicitScale(*n, *d);
}

std::filesystem::path numbered(const std::filesystem::path& p, std::size_t index) {
    if (index == 0) return p;
    auto copy = p;
    copy.replace_filename(p.stem().string() + "-" + std::to_string(index + 1) + p.extension().string());
    return copy;
}

} // namespace

int cmdDrawing(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"svg"}, {"dxf"}, {"paper"}, {"portrait", false}, {"scale"}, {"date"}});
    if (!parsed) return usageError(io, "drawing", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kDrawingHelp;
        return kExitOk;
    }
    if (a.positional.size() != 2) return usageError(io, "drawing", "expected <file.fosp> <item-id|part-id>.");
    if (!a.has("svg") && !a.has("dxf")) return usageError(io, "drawing", "give --svg out.svg and/or --dxf out.dxf.");
    const auto paper = paperByName(a.value("paper").value_or("A3"));
    if (!paper) return usageError(io, "drawing", "--paper must be A4, A3, A2 or A1.");
    const auto scale = scaleByText(a.value("scale").value_or("fit"));
    if (!scale) return usageError(io, "drawing", "--scale must be 'fit' or a ratio such as 1:10.");

    auto loaded = loadProjectOrReport(io, "drawing", a.positional[0]);
    if (!loaded) return kExitUsageOrIo;
    const auto& project = loaded->project;
    const auto& resolved = loaded->resolved;
    const std::string& id = a.positional[1];

    drawings::SheetOptions sheet;
    sheet.format = *paper;
    sheet.orientation = a.has("portrait") ? drawings::PaperOrientation::Portrait : drawings::PaperOrientation::Landscape;
    sheet.scale = *scale;
    sheet.title.project = project.metadata.name;
    sheet.title.author = project.metadata.author;
    sheet.title.date = a.value("date").value_or(project.metadata.modifiedAt.substr(0, 10));

    core::Result<drawings::DrawingResult> drawing = core::makeError(core::ErrorCode::NotFound, "");
    if (project.findItem(id)) {
        drawings::AssemblyDrawingOptions options;
        options.sheet = sheet;
        drawing = project::assemblyDrawing(project, resolved, id, options);
    } else {
        bool isPart = false;
        for (const auto& asmb : resolved.model.assemblies) isPart = isPart || asmb.findPart(domain::PartId(id));
        if (!isPart) return usageError(io, "drawing", "'" + id + "' is neither an item nor a generated part id.");
        drawings::PartDrawingOptions options;
        options.sheet = sheet;
        drawing = project::partDrawing(project, resolved, id, options);
    }
    if (!drawing) {
        io.err << "fos drawing: " << drawing.error().message << "\n";
        return kExitValidationErrors;
    }
    const auto& d = drawing.value();
    if (auto svgPath = a.value("svg")) {
        for (std::size_t i = 0; i < d.sheets.size(); ++i) {
            const auto path = numbered(*svgPath, i);
            if (!writeOutputFile(io, "drawing", path, drawings::toSvg(d.sheets[i]))) return kExitUsageOrIo;
            const auto& s = d.sheets[i];
            io.out << "Wrote " << path.string() << ": " << drawings::toString(*paper) << " "
                   << (a.has("portrait") ? "portrait" : "landscape") << " " << core::formatLength(s.width) << " x "
                   << core::formatLength(s.height) << " mm, scale " << s.scale.text() << ", " << s.views.size()
                   << " view(s)\n";
        }
    }
    if (auto dxfPath = a.value("dxf")) {
        if (d.views.empty()) {
            io.err << "fos drawing: the drawing has no view to export.\n";
            return kExitValidationErrors;
        }
        const auto& v = d.views.front();
        if (!writeOutputFile(io, "drawing", *dxfPath, drawings::viewToDxf(v.view, v.annotations)))
            return kExitUsageOrIo;
        io.out << "Wrote " << *dxfPath << ": view " << v.view.id << " at 1:1 (mm, DXF R12)\n";
    }
    if (!d.diagnostics.empty()) {
        io.out << "Diagnostics:\n";
        printDiagnostics(io.out, d.diagnostics);
    }
    return core::hasErrors(d.diagnostics) ? kExitValidationErrors : kExitOk;
}

} // namespace fos::cli::detail
