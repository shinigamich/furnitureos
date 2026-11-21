// SPDX-License-Identifier: AGPL-3.0-only
// DXF outline -> free panel item (shared by `fos import-dxf` and the desktop app).
#include "fos/project/dxf_panel.hpp"

#include "fos/furniture/contour.hpp"
#include "fos/furniture/panel.hpp"

#include <charconv>
#include <cmath>

namespace fos::project {
namespace {

/// Shortest decimal text that reads back as `v` (fixed notation: valid expression text).
std::string exact(double v) {
    if (std::fabs(v) < 1e-12) v = 0;
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed);
    return r.ec == std::errc() ? std::string(buf, r.ptr) : std::to_string(v);
}

core::Error refused(const std::string& message) {
    return core::makeError(core::ErrorCode::InvalidArgument, message);
}

/// The outline with every arc halved while it has fewer than 3 vertices (a custom contour
/// needs 3; a circle arrives as two semicircles). The geometry is unchanged: the new vertex is
/// the arc's apex and each half keeps the arc's side, bulge b -> b / (1 + sqrt(1 + b^2)).
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
            const geometry::Vec2 apex{(p.x + q.x) / 2 + v.bulge * (q.y - p.y) / 2,
                                      (p.y + q.y) / 2 - v.bulge * (q.x - p.x) / 2};
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

core::Result<domain::MaterialId> pickDxfPanelMaterial(const furniture::Libraries& libs,
                                                     const DxfPanelRequest& request) {
    if (!request.materialId.empty()) {
        const auto* m = libs.findMaterial(domain::MaterialId(request.materialId));
        if (!m) return refused("The project has no material '" + request.materialId + "'.");
        if (request.thickness && std::fabs(m->thickness - *request.thickness) > 1e-9)
            return refused("Material '" + request.materialId + "' is " + exact(m->thickness) + " mm thick, not " +
                           exact(*request.thickness) + " mm.");
        return m->id;
    }
    if (!request.thickness) return refused("Choose a material or a thickness for the panel.");
    const domain::Material* best = nullptr;
    for (const auto& m : libs.materials)
        if (std::fabs(m.thickness - *request.thickness) <= 1e-9 && (!best || m.id.value < best->id.value)) best = &m;
    if (!best) return refused("The project library has no material " + exact(*request.thickness) + " mm thick.");
    return best->id;
}

std::string dxfPanelItemId(const Project& p, const std::string& name) {
    std::string s;
    for (char c : name) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (ok) s += c;
        else if (!s.empty() && s.back() != '-') s += '-';
    }
    while (!s.empty() && s.back() == '-') s.pop_back();
    if (s.empty()) s = "panel";
    if (!p.findItem(s)) return s;
    for (int k = 2;; ++k)
        if (const std::string id = s + "-" + std::to_string(k); !p.findItem(id)) return id;
}

core::Result<DxfPanelImport> buildDxfPanelItem(const Project& project, const formats::DxfImport& dxf,
                                               const DxfPanelRequest& request, const std::string& itemId,
                                               const geometry::Transform& placement,
                                               core::Diagnostics* diagnosticsOut) {
    DxfPanelImport out;
    out.diagnostics = dxf.diagnostics;
    const auto fail = [&](const std::string& message) -> core::Result<DxfPanelImport> {
        if (diagnosticsOut) *diagnosticsOut = out.diagnostics;
        return refused(message);
    };
    if (request.name.empty()) return fail("The panel needs a name.");
    if (project.findItem(itemId)) return fail("Item id '" + itemId + "' already exists.");
    auto material = pickDxfPanelMaterial(project.libraries, request);
    if (!material) return fail(material.error().message);

    std::vector<geometry::Contour> loops;
    for (const auto& l : dxf.loops) loops.push_back(l.contour);
    auto shape = formats::importPanelShape(loops);
    if (!shape) return fail(shape.error().message);
    const formats::PanelShapeImport& s = shape.value();
    out.diagnostics.insert(out.diagnostics.end(), s.diagnostics.begin(), s.diagnostics.end());
    out.length = s.length;
    out.width = s.width;

    furniture::ContourSpec spec;
    spec.kind = furniture::ContourKind::Custom;
    const geometry::Contour outline = withAtLeastThreeVertices(s.shape.outer);
    for (const auto& v : outline.vertices) {
        spec.vertices.push_back({exact(v.point.x), exact(v.point.y), v.bulge, std::nullopt});
        if (v.bulge != 0.0) ++out.arcs;
    }
    out.outlineSegments = outline.size();
    std::size_t unsupported = 0;
    for (std::size_t i = 0; i < s.cutoutKinds.size(); ++i) {
        const auto& k = s.cutoutKinds[i];
        furniture::CutoutSpec c;
        c.x = exact(k.x);
        c.y = exact(k.y);
        if (k.kind == formats::CutoutPrimitive::Kind::Circle) {
            c.kind = furniture::CutoutKind::Circle;
            c.radius = exact(k.radius);
            ++out.circles;
        } else if (k.kind == formats::CutoutPrimitive::Kind::Rectangle) {
            c.kind = furniture::CutoutKind::Rectangle;
            c.length = exact(k.length);
            c.width = exact(k.width);
            c.radius = k.radius > 0 ? exact(k.radius) : "";
            ++out.rectangles;
        } else {
            const auto b = geometry::bounds(s.shape.cutouts[i]);
            const std::string what = "The cut-out at (" + exact(b.min.x) + ", " + exact(b.min.y) + ") .. (" +
                                     exact(b.max.x) + ", " + exact(b.max.y) + ") with " +
                                     std::to_string(s.shape.cutouts[i].size()) +
                                     " segments is neither a circle nor an axis-parallel (rounded) rectangle";
            out.diagnostics.push_back(
                {request.skipUnsupportedCutouts ? core::Severity::Warning : core::Severity::Error,
                 "project.import_dxf.cutout_unsupported",
                 what + (request.skipUnsupportedCutouts ? "; left out (" + request.skipOptionLabel + ")." : "."),
                 {}});
            ++unsupported;
            continue;
        }
        spec.cutouts.push_back(std::move(c));
    }
    if (unsupported > 0 && !request.skipUnsupportedCutouts)
        return fail(std::to_string(unsupported) +
                    " cut-out(s) of unsupported shape; " + request.skipOptionLabel + " imports the panel without them.");
    out.skipped = unsupported;

    furniture::PanelDefinition def =
        furniture::makePanel(domain::PartId(itemId + "/panel"), request.name, material.value());
    parametric::Transaction tx;
    tx.setValue(furniture::panel_param::kLength, s.length).setValue(furniture::panel_param::kWidth, s.width);
    if (auto r = def.parameters.apply(tx); !r) return fail(r.error().message);
    def.contour = spec;
    if (auto g = furniture::generatePanel(def, project.libraries); !g) return fail(g.error().message);
    out.material = material.value();
    out.item.definition = PanelItem{domain::AssemblyId(itemId), placement, std::move(def)};
    if (diagnosticsOut) *diagnosticsOut = out.diagnostics;
    return out;
}

} // namespace fos::project
