// SPDX-License-Identifier: AGPL-3.0-only
// `fos import-svg`: a free panel whose outline (and cut-outs) come from an SVG file.
#include "common.hpp"

#include "fos/formats/svg_import.hpp"
#include "fos/project/commands.hpp"
#include "fos/project/document.hpp"
#include "fos/project/dxf_panel.hpp"

#include <charconv>
#include <cmath>

namespace fos::cli::detail {
namespace {

const char* kImportSvgHelp =
    "Usage: fos import-svg <file.fosp> <outline.svg> (--material ID | --thickness T) [--name NAME]\n"
    "                      [--id ITEM-ID] [--scale MM-PER-UNIT] [--tolerance MM]\n"
    "                      [--skip-unsupported-cutouts] [-o OUT.fosp]\n\n"
    "Adds a free horizontal panel whose outline is read from an SVG file and saves the project\n"
    "(atomically) to OUT.fosp (default: the input project). Shapes: <path> with M L H V A Z\n"
    "(absolute and relative; arcs must be circular), <rect> (with rx/ry corners), <circle>,\n"
    "<ellipse> with rx = ry, <polygon>, <polyline>, <line>; transforms translate/scale/rotate/\n"
    "matrix. Bezier curves (C S Q T), elliptical arcs and skew transforms REFUSE the import:\n"
    "flatten curves first (Inkscape: Extensions > Modify Path > Flatten Beziers). Text, images\n"
    "and <use> references are not imported (reported); nothing is ever fetched from the network.\n"
    "Open pieces are joined end to end within --tolerance (default 0.01 mm). The largest closed\n"
    "outline is the panel; closed outlines inside it are cut-outs, which must be circles or\n"
    "axis-parallel rectangles (optionally with equal corner radii) unless\n"
    "--skip-unsupported-cutouts leaves the others out (reported).\n"
    "Units: the root width/height (mm, cm, in, pt, pc, px) over the viewBox; px and unitless\n"
    "values are 1/96 in (0.2646 mm). --scale gives the millimetres per SVG user unit instead.\n"
    "The drawing is read as seen (SVG's downward y is flipped).\n"
    "--material: material id from the project library; --thickness T alone picks the first\n"
    "library material (by id) of that thickness; with both, they must agree. --name defaults to\n"
    "the SVG file name; --id to a slug of the name (made unique).\n"
    "Exit codes: 0 imported, 1 refused (no closed outline, unsupported cut-out, invalid panel),\n"
    "2 usage error, unreadable, malformed or unsupported SVG, unreadable or unwritable project.\n";

std::string exact(double v) {
    if (std::fabs(v) < 1e-12) v = 0;
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed);
    return r.ec == std::errc() ? std::string(buf, r.ptr) : std::to_string(v);
}

} // namespace

int cmdImportSvg(const std::vector<std::string>& rawArgs, Io& io) {
    const char* cmd = "import-svg";
    std::vector<std::string> args;
    for (const auto& a : rawArgs) args.push_back(a == "-o" ? "--o" : a); // "-o out" = "--o out"
    auto parsed = parseArgs(args, {{"name"},
                                   {"id"},
                                   {"material"},
                                   {"thickness"},
                                   {"scale"},
                                   {"tolerance"},
                                   {"o"},
                                   {"skip-unsupported-cutouts", false}});
    if (!parsed) return usageError(io, cmd, parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kImportSvgHelp;
        return kExitOk;
    }
    if (a.positional.size() != 2) return usageError(io, cmd, "expected <file.fosp> <outline.svg>.");
    std::optional<double> thickness;
    if (auto t = a.value("thickness")) {
        thickness = parseNumber(*t);
        if (!thickness || !(*thickness > 0)) return usageError(io, cmd, "--thickness must be a number > 0, got '" + *t + "'.");
    }
    if (!a.value("material") && !thickness) return usageError(io, cmd, "give --material or --thickness.");
    formats::SvgImportOptions options;
    if (auto s = a.value("scale")) {
        const auto v = parseNumber(*s);
        if (!v || !(*v > 0 && *v <= 1e6)) return usageError(io, cmd, "--scale must be a number in (0, 1e6] mm per unit, got '" + *s + "'.");
        options.scale = *v;
    }
    if (auto t = a.value("tolerance")) {
        const auto v = parseNumber(*t);
        if (!v || !(*v > 0 && *v <= 10)) return usageError(io, cmd, "--tolerance must be a number in (0, 10] mm, got '" + *t + "'.");
        options.chainTolerance = *v;
    }
    const std::string& file = a.positional[0];
    const std::string& svgPath = a.positional[1];
    const std::string outFile = a.value("o").value_or(file);
    std::string name = a.value("name").value_or("");
    if (name.empty()) {
        const auto stem = pathFromUtf8(svgPath).stem().u8string();
        name = std::string(stem.begin(), stem.end());
    }
    if (name.empty()) return usageError(io, cmd, "--name is empty.");

    auto loaded = loadProjectOrReport(io, cmd, file);
    if (!loaded) return kExitUsageOrIo;
    const project::Project& proj = loaded->project;
    project::OutlinePanelRequest request;
    request.name = name;
    request.materialId = a.value("material").value_or("");
    request.thickness = thickness;
    request.skipUnsupportedCutouts = a.has("skip-unsupported-cutouts");
    request.skipOptionLabel = "--skip-unsupported-cutouts";
    request.diagnosticPrefix = "project.import_svg";
    auto materialId = project::pickDxfPanelMaterial(proj.libraries, request);
    if (!materialId) return usageError(io, cmd, materialId.error().message);
    const domain::Material* material = proj.libraries.findMaterial(materialId.value());
    std::string itemId = a.value("id").value_or("");
    if (!itemId.empty() && proj.findItem(itemId)) return usageError(io, cmd, "item id '" + itemId + "' already exists.");
    if (itemId.empty()) itemId = project::dxfPanelItemId(proj, name);

    auto svg = formats::importSvgFile(pathFromUtf8(svgPath), options);
    if (!svg) {
        io.err << "fos import-svg: cannot import '" << svgPath << "': " << svg.error().message << "\n";
        return kExitUsageOrIo;
    }
    core::Diagnostics diags;
    const auto refuse = [&](const std::string& message) {
        if (!diags.empty()) {
            io.err << "SVG diagnostics:\n";
            printDiagnostics(io.err, diags);
        }
        io.err << "fos import-svg: refused: " << message << "\nNothing was saved.\n";
        return kExitValidationErrors;
    };
    auto built = project::buildOutlinePanelItem(proj, formats::contoursOf(svg.value()), svg.value().diagnostics, request,
                                                itemId, geometry::Transform::identity(), &diags);
    if (!built) return refuse(built.error().message);
    project::OutlinePanelImport& imp = built.value();
    const std::size_t cutouts = imp.circles + imp.rectangles;

    project::Document doc(proj);
    if (auto r = doc.execute(project::commands::addItem(std::move(imp.item))); !r) return refuse(r.error().message);
    if (auto r = doc.save(pathFromUtf8(outFile)); !r) {
        io.err << "fos import-svg: cannot save '" << outFile << "': " << r.error().message << "\n";
        return kExitUsageOrIo;
    }
    io.out << "Imported '" << name << "' as item " << itemId << " (part " << itemId << "/panel), material "
           << material->id.value << " (" << exact(material->thickness) << " mm).\n"
           << "Outline: " << exact(imp.length) << " x " << exact(imp.width) << " mm, " << imp.outlineSegments
           << " segments (" << imp.arcs << " arcs); cut-outs: " << cutouts << " (" << imp.circles << " circle, "
           << imp.rectangles << " rectangle)";
    if (imp.skipped) io.out << ", " << imp.skipped << " left out";
    io.out << ".\nSVG: " << svg.value().shapeCount << " shape(s), units " << svg.value().unitSource << ", "
           << svg.value().loops.size() << " closed outline(s).\n";
    if (!diags.empty()) {
        io.out << "Diagnostics:\n";
        printDiagnostics(io.out, diags);
    }
    io.out << "Saved " << outFile << "\n";
    auto resolved = doc.resolve();
    if (resolved && core::hasErrors(resolved.value().diagnostics)) {
        io.err << "fos import-svg: saved, but the model has " << errorCount(resolved.value().diagnostics)
               << " error(s):\n";
        printDiagnostics(io.err, resolved.value().diagnostics);
        return kExitValidationErrors;
    }
    return kExitOk;
}

} // namespace fos::cli::detail
