// SPDX-License-Identifier: AGPL-3.0-only
// `fos import-dxf`: a free panel whose outline (and cut-outs) come from an ASCII DXF file.
#include "common.hpp"

#include "fos/formats/dxf_import.hpp"
#include "fos/furniture/contour.hpp"
#include "fos/furniture/panel.hpp"
#include "fos/project/commands.hpp"
#include "fos/project/document.hpp"

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

/// Shortest decimal text that reads back as `v` (fixed notation: valid expression text).
std::string exact(double v) {
    if (std::fabs(v) < 1e-12) v = 0;
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed);
    return r.ec == std::errc() ? std::string(buf, r.ptr) : std::to_string(v);
}

std::string slug(const std::string& name) {
    std::string s;
    for (char c : name) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (ok) s += c;
        else if (!s.empty() && s.back() != '-') s += '-';
    }
    while (!s.empty() && s.back() == '-') s.pop_back();
    return s.empty() ? "panel" : s;
}

std::string uniqueId(const project::Project& p, const std::string& base) {
    if (!p.findItem(base)) return base;
    for (int k = 2;; ++k)
        if (const std::string id = base + "-" + std::to_string(k); !p.findItem(id)) return id;
}

/// Material by id and/or thickness; nullptr with `why` set when there is none.
const domain::Material* pickMaterial(const furniture::Libraries& libs, const std::optional<std::string>& id,
                                     std::optional<double> thickness, std::string& why) {
    if (id) {
        const auto* m = libs.findMaterial(domain::MaterialId(*id));
        if (!m) why = "the project has no material '" + *id + "'.";
        else if (thickness && std::fabs(m->thickness - *thickness) > 1e-9)
            why = "material '" + *id + "' is " + exact(m->thickness) + " mm thick, not " + exact(*thickness) + " mm.";
        else return m;
        return nullptr;
    }
    const domain::Material* best = nullptr;
    for (const auto& m : libs.materials)
        if (std::fabs(m.thickness - *thickness) <= 1e-9 && (!best || m.id.value < best->id.value)) best = &m;
    if (!best) why = "the project library has no material " + exact(*thickness) + " mm thick.";
    return best;
}

/// The outline with every arc halved while it has fewer than 3 vertices (a custom contour
/// needs 3; a circle arrives as two semicircles). The geometry is unchanged.
geometry::Contour withAtLeastThreeVertices(geometry::Contour c) {
    while (c.size() < 3) {
        geometry::Contour out;
        for (std::size_t i = 0; i < c.size(); ++i) {
            const auto& v = c.vertices[i];
            if (v.bulge == 0.0) {
                out.vertices.push_back(v);
                continue;
            }
            const geometry::Vec2 p = v.point, q = c.vertices[(i + 1) % c.size()].point;
            const geometry::Vec2 apex{(p.x + q.x) / 2 + v.bulge * (q.y - p.y) / 2, (p.y + q.y) / 2 - v.bulge * (q.x - p.x) / 2};
            const double half = v.bulge / (1.0 + std::sqrt(1.0 + v.bulge * v.bulge));
            out.vertices.push_back({p, half});
            out.vertices.push_back({apex, half});
        }
        if (out.size() == c.size()) break; // no arc to split (cannot happen for a valid contour)
        c = std::move(out);
    }
    return c;
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
    std::string why;
    const domain::Material* material = pickMaterial(proj.libraries, a.value("material"), thickness, why);
    if (!material) return usageError(io, cmd, why);
    std::string itemId = a.value("id").value_or("");
    if (!itemId.empty() && proj.findItem(itemId)) return usageError(io, cmd, "item id '" + itemId + "' already exists.");
    if (itemId.empty()) itemId = uniqueId(proj, slug(*name));

    auto dxf = formats::importDxfFile(pathFromUtf8(dxfPath), options);
    if (!dxf) {
        io.err << "fos import-dxf: cannot import '" << dxfPath << "': " << dxf.error().message << "\n";
        return kExitUsageOrIo;
    }
    core::Diagnostics diags = dxf.value().diagnostics;
    std::vector<geometry::Contour> loops;
    for (const auto& l : dxf.value().loops) loops.push_back(l.contour);
    const auto refuse = [&](const std::string& message) {
        if (!diags.empty()) {
            io.err << "DXF diagnostics:\n";
            printDiagnostics(io.err, diags);
        }
        io.err << "fos import-dxf: refused: " << message << "\nNothing was saved.\n";
        return kExitValidationErrors;
    };
    auto shape = formats::importPanelShape(loops);
    if (!shape) return refuse(shape.error().message);
    const formats::PanelShapeImport& s = shape.value();
    diags.insert(diags.end(), s.diagnostics.begin(), s.diagnostics.end());

    furniture::ContourSpec spec;
    spec.kind = furniture::ContourKind::Custom;
    std::size_t arcs = 0;
    const geometry::Contour outline = withAtLeastThreeVertices(s.shape.outer);
    for (const auto& v : outline.vertices) {
        spec.vertices.push_back({exact(v.point.x), exact(v.point.y), v.bulge, std::nullopt});
        if (v.bulge != 0.0) ++arcs;
    }
    std::size_t circles = 0, rects = 0, skipped = 0;
    std::vector<std::string> unsupported;
    for (std::size_t i = 0; i < s.cutoutKinds.size(); ++i) {
        const auto& k = s.cutoutKinds[i];
        furniture::CutoutSpec c;
        c.x = exact(k.x);
        c.y = exact(k.y);
        if (k.kind == formats::CutoutPrimitive::Kind::Circle) {
            c.kind = furniture::CutoutKind::Circle;
            c.radius = exact(k.radius);
            ++circles;
        } else if (k.kind == formats::CutoutPrimitive::Kind::Rectangle) {
            c.kind = furniture::CutoutKind::Rectangle;
            c.length = exact(k.length);
            c.width = exact(k.width);
            c.radius = k.radius > 0 ? exact(k.radius) : "";
            ++rects;
        } else {
            const auto b = geometry::bounds(s.shape.cutouts[i]);
            unsupported.push_back("cut-out at (" + exact(b.min.x) + ", " + exact(b.min.y) + ") .. (" + exact(b.max.x) +
                                  ", " + exact(b.max.y) + ") with " + std::to_string(s.shape.cutouts[i].size()) +
                                  " segments is neither a circle nor an axis-parallel (rounded) rectangle");
            continue;
        }
        spec.cutouts.push_back(std::move(c));
    }
    for (const auto& u : unsupported) {
        diags.push_back({a.has("skip-unsupported-cutouts") ? core::Severity::Warning : core::Severity::Error,
                         "cli.import_dxf.cutout_unsupported",
                         "The " + u + (a.has("skip-unsupported-cutouts") ? "; left out (--skip-unsupported-cutouts)." : "."),
                         {}});
        ++skipped;
    }
    if (!unsupported.empty() && !a.has("skip-unsupported-cutouts"))
        return refuse(std::to_string(unsupported.size()) +
                      " cut-out(s) of unsupported shape; --skip-unsupported-cutouts imports the panel without them.");

    furniture::PanelDefinition def = furniture::makePanel(domain::PartId(itemId + "/panel"), *name, material->id);
    parametric::Transaction tx;
    tx.setValue(furniture::panel_param::kLength, s.length).setValue(furniture::panel_param::kWidth, s.width);
    if (auto r = def.parameters.apply(tx); !r) return refuse(r.error().message);
    def.contour = spec;
    if (auto g = furniture::generatePanel(def, proj.libraries); !g) return refuse(g.error().message);

    project::Item item;
    item.definition = project::PanelItem{domain::AssemblyId(itemId), geometry::Transform::identity(), std::move(def)};
    project::Document doc(proj);
    if (auto r = doc.execute(project::commands::addItem(std::move(item))); !r) return refuse(r.error().message);
    if (auto r = doc.save(pathFromUtf8(file)); !r) {
        io.err << "fos import-dxf: cannot save '" << file << "': " << r.error().message << "\n";
        return kExitUsageOrIo;
    }
    io.out << "Imported '" << *name << "' as item " << itemId << " (part " << itemId << "/panel), material "
           << material->id.value << " (" << exact(material->thickness) << " mm).\n"
           << "Outline: " << exact(s.length) << " x " << exact(s.width) << " mm, " << outline.size()
           << " segments (" << arcs << " arcs); cut-outs: " << spec.cutouts.size() << " (" << circles << " circle, "
           << rects << " rectangle)";
    if (skipped) io.out << ", " << skipped << " left out";
    io.out << ".\nDXF: " << dxf.value().entityCount << " entities, unit " << formats::toString(dxf.value().unit)
           << ", " << loops.size() << " closed outline(s).\n";
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
