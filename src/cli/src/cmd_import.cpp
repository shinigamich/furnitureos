// SPDX-License-Identifier: AGPL-3.0-only
// `fos import-dxf`: a free panel whose outline (and cut-outs) come from an ASCII DXF file.
#include "common.hpp"

#include "fos/formats/dxf_import.hpp"
#include "fos/project/commands.hpp"
#include "fos/project/document.hpp"
#include "fos/project/dxf_panel.hpp"

#include <charconv>
#include <cmath>

namespace fos::cli::detail {
namespace {

const char* kImportDxfHelp =
    "Usage: fos import-dxf <file.fosp> <outline.dxf> --name NAME (--material ID | --thickness T)\n"
    "                      [--id ITEM-ID] [--layer LAYER] [--units mm|cm|m|in|ft] [--tolerance MM]\n"
    "                      [--skip-unsupported-cutouts]\n\n"
    "Adds a free horizontal panel whose outline is read from an ASCII DXF file (R12 or later) and\n"
    "saves the project (atomically). Entities: LINE, ARC, CIRCLE, LWPOLYLINE and 2D POLYLINE;\n"
    "loose lines and arcs are joined end to end within --tolerance (default 0.01 mm). Blocks\n"
    "(INSERT), splines, ellipses, text and dimensions are NOT imported (reported). The largest\n"
    "closed outline is the panel; closed outlines inside it are cut-outs, which must be circles\n"
    "or axis-parallel rectangles (optionally with equal corner radii); other cut-out shapes refuse\n"
    "the import unless --skip-unsupported-cutouts leaves them out (reported).\n"
    "Units: the file's $INSUNITS, or --units; a file without units is read as mm (warning).\n"
    "--material: material id from the project library (its thickness is the panel's);\n"
    "--thickness T alone picks the first library material (by id) of that thickness; with both,\n"
    "they must agree. --id defaults to a slug of the name (made unique).\n"
    "The outline is stored as a custom parametric contour (numbers, editable later); length and\n"
    "width are the outline's bounding box, the panel sits at x = y = z = 0.\n"
    "Exit codes: 0 imported, 1 refused (no closed outline, unsupported cut-out, invalid panel),\n"
    "2 usage error, unreadable or malformed DXF, unreadable or unwritable project.\n";

/// Shortest decimal text that reads back as `v` (fixed notation).
std::string exact(double v) {
    if (std::fabs(v) < 1e-12) v = 0;
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed);
    return r.ec == std::errc() ? std::string(buf, r.ptr) : std::to_string(v);
}

} // namespace

int cmdImportDxf(const std::vector<std::string>& args, Io& io) {
    const char* cmd = "import-dxf";
    auto parsed = parseArgs(args, {{"name"},
                                   {"id"},
                                   {"material"},
                                   {"thickness"},
                                   {"layer"},
                                   {"units"},
                                   {"tolerance"},
                                   {"skip-unsupported-cutouts", false}});
    if (!parsed) return usageError(io, cmd, parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kImportDxfHelp;
        return kExitOk;
    }
    if (a.positional.size() != 2) return usageError(io, cmd, "expected <file.fosp> <outline.dxf>.");
    const auto name = a.value("name");
    if (!name || name->empty()) return usageError(io, cmd, "--name is required.");
    std::optional<double> thickness;
    if (auto t = a.value("thickness")) {
        thickness = parseNumber(*t);
        if (!thickness || !(*thickness > 0)) return usageError(io, cmd, "--thickness must be a number > 0, got '" + *t + "'.");
    }
    if (!a.value("material") && !thickness) return usageError(io, cmd, "give --material or --thickness.");
    formats::DxfImportOptions options;
    if (auto u = a.value("units")) {
        options.units = formats::dxfUnitFromString(*u);
        if (!options.units) return usageError(io, cmd, "--units must be mm, cm, m, in or ft, got '" + *u + "'.");
    }
    if (auto t = a.value("tolerance")) {
        const auto v = parseNumber(*t);
        if (!v || !(*v > 0 && *v <= 10)) return usageError(io, cmd, "--tolerance must be a number in (0, 10] mm, got '" + *t + "'.");
        options.chainTolerance = *v;
    }
    options.layer = a.value("layer").value_or("");
    const std::string& file = a.positional[0];
    const std::string& dxfPath = a.positional[1];

    auto loaded = loadProjectOrReport(io, cmd, file);
    if (!loaded) return kExitUsageOrIo;
    const project::Project& proj = loaded->project;
    project::DxfPanelRequest request;
    request.name = *name;
    request.materialId = a.value("material").value_or("");
    request.thickness = thickness;
    request.skipUnsupportedCutouts = a.has("skip-unsupported-cutouts");
    request.skipOptionLabel = "--skip-unsupported-cutouts";
    auto materialId = project::pickDxfPanelMaterial(proj.libraries, request);
    if (!materialId) return usageError(io, cmd, materialId.error().message);
    const domain::Material* material = proj.libraries.findMaterial(materialId.value());
    std::string itemId = a.value("id").value_or("");
    if (!itemId.empty() && proj.findItem(itemId)) return usageError(io, cmd, "item id '" + itemId + "' already exists.");
    if (itemId.empty()) itemId = project::dxfPanelItemId(proj, *name);

    auto dxf = formats::importDxfFile(pathFromUtf8(dxfPath), options);
    if (!dxf) {
        io.err << "fos import-dxf: cannot import '" << dxfPath << "': " << dxf.error().message << "\n";
        return kExitUsageOrIo;
    }
    core::Diagnostics diags;
    const auto refuse = [&](const std::string& message) {
        if (!diags.empty()) {
            io.err << "DXF diagnostics:\n";
            printDiagnostics(io.err, diags);
        }
        io.err << "fos import-dxf: refused: " << message << "\nNothing was saved.\n";
        return kExitValidationErrors;
    };
    auto built = project::buildDxfPanelItem(proj, dxf.value(), request, itemId, geometry::Transform::identity(), &diags);
    if (!built) return refuse(built.error().message);
    project::DxfPanelImport& imp = built.value();
    const std::size_t cutouts = imp.circles + imp.rectangles;

    project::Document doc(proj);
    if (auto r = doc.execute(project::commands::addItem(std::move(imp.item))); !r) return refuse(r.error().message);
    if (auto r = doc.save(pathFromUtf8(file)); !r) {
        io.err << "fos import-dxf: cannot save '" << file << "': " << r.error().message << "\n";
        return kExitUsageOrIo;
    }
    io.out << "Imported '" << *name << "' as item " << itemId << " (part " << itemId << "/panel), material "
           << material->id.value << " (" << exact(material->thickness) << " mm).\n"
           << "Outline: " << exact(imp.length) << " x " << exact(imp.width) << " mm, " << imp.outlineSegments
           << " segments (" << imp.arcs << " arcs); cut-outs: " << cutouts << " (" << imp.circles << " circle, "
           << imp.rectangles << " rectangle)";
    if (imp.skipped) io.out << ", " << imp.skipped << " left out";
    io.out << ".\nDXF: " << dxf.value().entityCount << " entities, unit " << formats::toString(dxf.value().unit)
           << ", " << dxf.value().loops.size() << " closed outline(s).\n";
    if (!diags.empty()) {
        io.out << "Diagnostics:\n";
        printDiagnostics(io.out, diags);
    }
    io.out << "Saved " << file << "\n";
    auto resolved = doc.resolve();
    if (resolved && core::hasErrors(resolved.value().diagnostics)) {
        io.err << "fos import-dxf: saved, but the model has " << errorCount(resolved.value().diagnostics)
               << " error(s):\n";
        printDiagnostics(io.err, resolved.value().diagnostics);
        return kExitValidationErrors;
    }
    return kExitOk;
}

} // namespace fos::cli::detail
