// SPDX-License-Identifier: AGPL-3.0-only
// fos drawing
#include "common.hpp"

#include "fos/core/units.hpp"
#include "fos/drawings/dxf.hpp"
#include "fos/drawings/pictorial.hpp"
#include "fos/drawings/svg.hpp"
#include "fos/project/outputs.hpp"

namespace fos::cli::detail {
namespace {

const char* kDrawingHelp =
    "Usage: fos drawing <file.fosp> <item-id|part-id> (--svg out.svg | --dxf out.dxf)\n"
    "                   [--paper A4|A3|A2|A1] [--portrait] [--scale 1:N|N:M|fit] [--date TEXT]\n"
    "                   [--view iso|exploded [--explode F] [--balloons]]\n\n"
    "An item id gives the assembly drawing (front view, top view below, left view to the\n"
    "right - ISO first angle - with automatic dimensions); a part id (e.g. cab1/side.left)\n"
    "gives the part drawing (face A, holes, edge banding, finished and cut sizes). Stored\n"
    "drawing edits of the project are applied; stale text overrides are reported.\n"
    "--svg: the sheet at exact scale (width/height in mm, 1 user unit = 1 mm, 100 mm\n"
    "calibration line); further sheets get -2, -3, ... before the extension.\n"
    "--dxf: the first view (front view / face A) in model space at 1:1 in mm (DXF R12).\n"
    "Defaults: --paper A3, landscape, --scale fit (largest of 1:1 1:2 1:5 1:10 1:20 1:50 1:100\n"
    "that fits). An explicit scale that does not fit is an error. --date is printed in the\n"
    "title block (default: the date of the project's last modification).\n"
    "--view iso|exploded (items only): one sheet with the isometric (or exploded isometric)\n"
    "assembly view instead of the three views; hidden lines omitted, no dimensions. --explode F\n"
    "(1..10, default 1.5) moves sides out in X, top/bottom in Z, back backwards and fronts\n"
    "forwards by (F - 1) x the assembly size; --balloons adds part-list numbers. --dxf then\n"
    "writes this view in model space at 1:1.\n"
    "Exit code 1 when the drawing has errors or the project has model errors (e.g. an item that\n"
    "cannot be generated is missing from the drawings; stderr says which items).\n";

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
    std::filesystem::path name = p.stem(); // path arithmetic, no narrow-string round trip
    name += "-" + std::to_string(index + 1);
    name += p.extension();
    copy.replace_filename(name);
    return copy;
}

} // namespace

int cmdDrawing(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"svg"}, {"dxf"}, {"paper"}, {"portrait", false}, {"scale"}, {"date"}, {"view"}, {"explode"}, {"balloons", false}});
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

    const std::string viewName = a.value("view").value_or("");
    if (!viewName.empty() && viewName != "iso" && viewName != "exploded")
        return usageError(io, "drawing", "--view must be iso or exploded.");
    if (viewName.empty() && (a.has("explode") || a.has("balloons")))
        return usageError(io, "drawing", "--explode and --balloons need --view iso|exploded.");
    std::optional<double> explode;
    if (auto e = a.value("explode")) {
        explode = parseNumber(*e);
        if (!explode || viewName != "exploded")
            return usageError(io, "drawing", "--explode needs --view exploded and a number from 1 to 10.");
    }

    auto loaded = loadProjectOrReport(io, "drawing", a.positional[0]);
    if (!loaded) return kExitUsageOrIo;
    const auto& project = loaded->project;
    const auto& resolved = loaded->resolved;
    const std::string& id = a.positional[1];
    const std::string incomplete = resolveErrorNotice("drawing", *loaded);

    drawings::SheetOptions sheet;
    sheet.format = *paper;
    sheet.orientation = a.has("portrait") ? drawings::PaperOrientation::Portrait : drawings::PaperOrientation::Landscape;
    sheet.scale = *scale;
    sheet.title.project = project.metadata.name;
    sheet.title.author = project.metadata.author;
    sheet.title.date = a.value("date").value_or(project.metadata.modifiedAt.substr(0, 10));

    if (!viewName.empty()) {
        if (!project.findItem(id))
            return usageError(io, "drawing", "--view iso|exploded needs an item id; '" + id + "' is not an item.");
        drawings::PictorialDrawingOptions options;
        options.sheet = sheet;
        options.view.kind = viewName == "exploded" ? drawings::PictorialKind::Exploded : drawings::PictorialKind::Isometric;
        if (explode) options.view.explodeFactor = *explode;
        options.render.balloons = a.has("balloons");
        if (resolved.model.findAssembly(domain::AssemblyId(id)) == nullptr) {
            io.err << "fos drawing: item \"" << id << "\" has no generated assembly to draw.\n" << incomplete;
            return kExitValidationErrors;
        }
        auto pic = drawings::generatePictorialDrawing(resolved.model, domain::AssemblyId(id), options);
        if (!pic) {
            io.err << "fos drawing: " << pic.error().message << "\n" << incomplete;
            return kExitValidationErrors;
        }
        const auto& p = pic.value();
        if (auto svgPath = a.value("svg")) {
            if (!writeOutputFile(io, "drawing", pathFromUtf8(*svgPath), drawings::toSvg(p.sheet))) return kExitUsageOrIo;
            io.out << "Wrote " << *svgPath << ": " << viewName << " view, scale " << p.sheet.scale.text() << "\n";
        }
        if (auto dxfPath = a.value("dxf")) {
            drawings::DxfOptions dxf;
            dxf.linetypeScale = 10.0;
            if (!writeOutputFile(io, "drawing", pathFromUtf8(*dxfPath),
                                 drawings::toDxf(drawings::pictorialModelSpacePrimitives(p.view, options.render, 10.0), dxf)))
                return kExitUsageOrIo;
            io.out << "Wrote " << *dxfPath << ": view " << p.view.id << " at 1:1 (mm, DXF R12)\n";
        }
        if (!p.diagnostics.empty()) {
            io.out << "Diagnostics:\n";
            printDiagnostics(io.out, p.diagnostics);
        }
        io.err << incomplete;
        return core::hasErrors(p.diagnostics) || !incomplete.empty() ? kExitValidationErrors : kExitOk;
    }

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
        io.err << "fos drawing: " << drawing.error().message << "\n" << incomplete;
        return kExitValidationErrors;
    }
    const auto& d = drawing.value();
    if (auto svgPath = a.value("svg")) {
        for (std::size_t i = 0; i < d.sheets.size(); ++i) {
            const auto path = numbered(pathFromUtf8(*svgPath), i);
            if (!writeOutputFile(io, "drawing", path, drawings::toSvg(d.sheets[i]))) return kExitUsageOrIo;
            const auto& s = d.sheets[i];
            io.out << "Wrote " << pathToUtf8(path) << ": " << drawings::toString(*paper) << " "
                   << (a.has("portrait") ? "portrait" : "landscape") << " " << core::formatLength(s.width) << " x "
                   << core::formatLength(s.height) << " mm, scale " << s.scale.text() << ", " << s.views.size()
                   << " view(s)\n";
        }
    }
    if (auto dxfPath = a.value("dxf")) {
        if (d.views.empty()) {
            io.err << "fos drawing: the drawing has no view to export.\n" << incomplete;
            return kExitValidationErrors;
        }
        const auto& v = d.views.front();
        if (!writeOutputFile(io, "drawing", pathFromUtf8(*dxfPath), drawings::viewToDxf(v.view, v.annotations)))
            return kExitUsageOrIo;
        io.out << "Wrote " << *dxfPath << ": view " << v.view.id << " at 1:1 (mm, DXF R12)\n";
    }
    if (!d.diagnostics.empty()) {
        io.out << "Diagnostics:\n";
        printDiagnostics(io.out, d.diagnostics);
    }
    io.err << incomplete;
    return core::hasErrors(d.diagnostics) || !incomplete.empty() ? kExitValidationErrors : kExitOk;
}

} // namespace fos::cli::detail
