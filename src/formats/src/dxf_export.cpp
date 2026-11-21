// SPDX-License-Identifier: AGPL-3.0-only
// DXF R12 part outline export (see fos/formats/dxf_export.hpp).
#include "fos/formats/dxf_export.hpp"

#include "fos/geometry/contour.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <variant>

namespace fos::formats {
namespace {

/// Shortest fixed-notation text that reads back as `v` (no exponent: every DXF reader parses it).
std::string num(double v) {
    if (std::fabs(v) < 1e-12) v = 0;
    char buf[400];
    const auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed);
    return r.ec == std::errc() ? std::string(buf, r.ptr) : std::to_string(v);
}

/// ASCII-only DXF text: printable ASCII kept, control characters dropped, UTF-8 code points
/// written as \U+XXXX (the AutoCAD unicode escape; invalid bytes become '?').
std::string dxfText(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            if (c >= 0x20 && c != 0x7f) out += static_cast<char>(c);
            ++i;
            continue;
        }
        const std::size_t n = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 0;
        std::uint32_t cp = n == 4 ? (c & 0x07u) : n == 3 ? (c & 0x0fu) : (c & 0x1fu);
        bool ok = n != 0 && i + n <= s.size();
        for (std::size_t k = 1; ok && k < n; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            ok = (cc & 0xc0u) == 0x80u;
            cp = (cp << 6) | (cc & 0x3fu);
        }
        if (!ok || cp > 0xffff) {
            out += '?';
            i += ok ? n : 1;
            continue;
        }
        char buf[16];
        std::snprintf(buf, sizeof buf, "\\U+%04X", static_cast<unsigned>(cp));
        out += buf;
        i += n;
    }
    return out;
}

core::Error invalid(const std::string& message) { return core::makeError(core::ErrorCode::InvalidArgument, message); }

std::string partLabel(const domain::Part& p) { return "'" + p.name + "' (" + p.id.value + ")"; }

std::string drillLayer(double diameter) {
    std::string d = num(diameter);
    std::replace(d.begin(), d.end(), '.', '_');
    return "DRILL_" + d;
}

int layerColour(const std::string& layer) {
    if (layer == "OUTLINE") return 7;
    if (layer == "CUTOUTS") return 1;
    if (layer == "POCKETS") return 6;
    if (layer == "GROOVES") return 3;
    if (layer == "LABELS") return 8;
    return 5; // DRILL_*
}

/// Entities of one drawing plus the layers and extents they use.
class Writer {
public:
    void polyline(const std::string& layer, const geometry::Contour& c, geometry::Vec2 off) {
        use(layer);
        pair(0, "POLYLINE"), pair(8, layer), pair(66, "1"), pair(10, "0"), pair(20, "0"), pair(30, "0"), pair(70, "1");
        for (const auto& v : c.vertices) {
            const geometry::Vec2 p{v.point.x + off.x, v.point.y + off.y};
            pair(0, "VERTEX"), pair(8, layer), pair(10, num(p.x)), pair(20, num(p.y)), pair(30, "0");
            if (v.bulge != 0.0) pair(42, num(v.bulge));
        }
        pair(0, "SEQEND"), pair(8, layer);
        const auto b = geometry::bounds(c);
        extend({b.min.x + off.x, b.min.y + off.y});
        extend({b.max.x + off.x, b.max.y + off.y});
    }
    void circle(const std::string& layer, geometry::Vec2 centre, double radius) {
        use(layer);
        pair(0, "CIRCLE"), pair(8, layer), pair(10, num(centre.x)), pair(20, num(centre.y)), pair(30, "0"),
            pair(40, num(radius));
        extend({centre.x - radius, centre.y - radius});
        extend({centre.x + radius, centre.y + radius});
    }
    void text(const std::string& layer, geometry::Vec2 at, double height, const std::string& s) {
        use(layer);
        pair(0, "TEXT"), pair(8, layer), pair(10, num(at.x)), pair(20, num(at.y)), pair(30, "0"),
            pair(40, num(height)), pair(1, dxfText(s));
        extend(at);
        extend({at.x, at.y + height});
    }

    /// The complete file.
    std::string document() const {
        std::string out;
        const auto p = [&](int code, const std::string& value) { out += std::to_string(code) + "\n" + value + "\n"; };
        p(0, "SECTION"), p(2, "HEADER"), p(9, "$ACADVER"), p(1, "AC1009"), p(9, "$INSUNITS"), p(70, "4");
        p(9, "$EXTMIN"), p(10, num(min_.x)), p(20, num(min_.y)), p(30, "0");
        p(9, "$EXTMAX"), p(10, num(max_.x)), p(20, num(max_.y)), p(30, "0");
        p(0, "ENDSEC");
        p(0, "SECTION"), p(2, "TABLES");
        p(0, "TABLE"), p(2, "LTYPE"), p(70, "1");
        p(0, "LTYPE"), p(2, "CONTINUOUS"), p(70, "0"), p(3, "Solid line"), p(72, "65"), p(73, "0"), p(40, "0.0");
        p(0, "ENDTAB");
        p(0, "TABLE"), p(2, "LAYER"), p(70, std::to_string(layers_.size() + 1));
        p(0, "LAYER"), p(2, "0"), p(70, "0"), p(62, "7"), p(6, "CONTINUOUS");
        for (const auto& l : layers_)
            p(0, "LAYER"), p(2, l), p(70, "0"), p(62, std::to_string(layerColour(l))), p(6, "CONTINUOUS");
        p(0, "ENDTAB"), p(0, "ENDSEC");
        p(0, "SECTION"), p(2, "BLOCKS"), p(0, "ENDSEC");
        p(0, "SECTION"), p(2, "ENTITIES");
        out += entities_;
        p(0, "ENDSEC"), p(0, "EOF");
        return out;
    }

private:
    int pair(int code, const std::string& value) {
        entities_ += std::to_string(code) + "\n" + value + "\n";
        return 0;
    }
    void use(const std::string& layer) { layers_.insert(layer); }
    void extend(geometry::Vec2 p) {
        if (!any_) min_ = max_ = p;
        min_ = {std::min(min_.x, p.x), std::min(min_.y, p.y)};
        max_ = {std::max(max_.x, p.x), std::max(max_.y, p.y)};
        any_ = true;
    }
    std::string entities_;
    std::set<std::string> layers_; // sorted: deterministic table order
    geometry::Vec2 min_{0, 0}, max_{0, 0};
    bool any_ = false;
};

geometry::Contour polygonContour(const std::vector<geometry::Vec2>& pts) {
    geometry::Contour c;
    for (const auto& p : pts) c.vertices.push_back({p, 0.0});
    return c;
}

/// Draws `part` into `w` shifted by `off`; appends warnings to `diags`.
core::Result<void> drawPart(Writer& w, const domain::Part& part, const domain::Model& model,
                            const DxfExportOptions& options, geometry::Vec2 off, core::Diagnostics& diags) {
    geometry::Contour outline = domain::finishedContour(part);
    if (!options.finishedOutline) {
        auto raw = domain::cutContour(part, model);
        if (!raw) return invalid("Part " + partLabel(part) + ": the raw outline cannot be computed: " + raw.error().message);
        outline = raw.value();
    }
    if (auto v = geometry::validateContour(outline); !v)
        return invalid("Part " + partLabel(part) + ": the outline is not a valid contour: " + v.error().message);
    const auto bf = geometry::bounds(domain::finishedContour(part));
    w.polyline("OUTLINE", outline, off);
    if (part.shape)
        for (const auto& c : part.shape->cutouts) w.polyline("CUTOUTS", c, off);

    std::size_t skippedHoles = 0, skippedGrooves = 0, skippedPockets = 0;
    const auto badOp = [&](const domain::Operation& op, const std::string& why) {
        diags.push_back({core::Severity::Warning, "formats.dxf_export.invalid_operation",
                         "Operation " + op.id.value + " of part " + partLabel(part) + " " + why + "; not exported.",
                         {}});
    };
    for (const auto& op : part.operations) {
        if (const auto* h = std::get_if<domain::Hole>(&op.shape)) {
            if (!(h->face == domain::Face::A || (h->face == domain::Face::B && h->through))) {
                ++skippedHoles;
                continue;
            }
            if (!(h->diameter > 0) || !std::isfinite(h->diameter)) {
                badOp(op, "is a hole of diameter " + num(h->diameter) + " mm");
                continue;
            }
            w.circle(drillLayer(h->diameter), {h->u + off.x, h->v + off.y}, h->diameter / 2);
        } else if (const auto* g = std::get_if<domain::Groove>(&op.shape)) {
            if (g->face != domain::Face::A) {
                ++skippedGrooves;
                continue;
            }
            const double dx = g->u2 - g->u1, dy = g->v2 - g->v1, len = std::hypot(dx, dy);
            if (!(len > 1e-9) || !(g->width > 0)) {
                badOp(op, "is a groove " + num(len) + " mm long and " + num(g->width) + " mm wide");
                continue;
            }
            const double nx = -dy / len * g->width / 2, ny = dx / len * g->width / 2;
            w.polyline("GROOVES",
                       polygonContour({{g->u1 - nx, g->v1 - ny}, {g->u2 - nx, g->v2 - ny}, {g->u2 + nx, g->v2 + ny},
                                       {g->u1 + nx, g->v1 + ny}}),
                       off);
        } else if (const auto* pk = std::get_if<domain::Pocket>(&op.shape)) {
            const bool shown = pk->face == domain::Face::A || (pk->face == domain::Face::B && pk->through);
            if (!shown) {
                ++skippedPockets;
                continue;
            }
            if (pk->contour.size() < 3) {
                badOp(op, "is a pocket with " + std::to_string(pk->contour.size()) + " vertices");
                continue;
            }
            w.polyline(pk->through ? "CUTOUTS" : "POCKETS", polygonContour(pk->contour), off);
        }
    }
    if (skippedHoles + skippedGrooves + skippedPockets > 0)
        diags.push_back({core::Severity::Warning, "formats.dxf_export.operations_skipped",
                         "Part " + partLabel(part) + ": " + std::to_string(skippedHoles) + " hole(s), " +
                             std::to_string(skippedGrooves) + " groove(s) and " + std::to_string(skippedPockets) +
                             " pocket(s) on face B (blind) or an edge face are not in the DXF (drawn seen from face A).",
                         {}});
    w.text("LABELS", {bf.min.x + off.x, bf.max.y + off.y + options.textHeight / 2}, options.textHeight,
           part.id.value);
    return {};
}

std::string safeName(const std::string& id) {
    std::string s = id;
    for (char& c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                        c == '_' || c == '-';
        if (!ok) c = '_';
    }
    return s.empty() ? "_" : s;
}

} // namespace

core::Result<DxfExport> exportPartOutlinesDxf(const domain::Model& model, const DxfExportOptions& options) {
    if (!(options.spacing >= 0) || !std::isfinite(options.spacing))
        return invalid("DXF export: spacing must be a finite number >= 0 mm, got " + num(options.spacing) + ".");
    if (!(options.textHeight > 0) || !std::isfinite(options.textHeight))
        return invalid("DXF export: text height must be a finite number > 0 mm, got " + num(options.textHeight) + ".");
    std::vector<const domain::Part*> parts;
    for (const auto& a : model.assemblies)
        for (const auto& p : a.parts)
            if (options.partId.empty() || p.id.value == options.partId) parts.push_back(&p);
    if (parts.empty())
        return core::makeError(core::ErrorCode::NotFound, options.partId.empty()
                                                              ? "DXF export: the model has no parts."
                                                              : "DXF export: the model has no part '" + options.partId + "'.");
    DxfExport out;
    out.partCount = parts.size();
    if (options.oneFilePerPart) {
        std::set<std::string> used;
        for (const auto* p : parts) {
            Writer w;
            if (auto r = drawPart(w, *p, model, options, {0, 0}, out.diagnostics); !r) return r.error();
            std::string base = safeName(p->id.value), name = base + ".dxf";
            for (int k = 2; used.count(name); ++k) name = base + "-" + std::to_string(k) + ".dxf";
            used.insert(name);
            out.files.push_back({p->id.value, name, w.document()});
        }
        return out;
    }
    Writer w;
    double cursor = 0;
    for (const auto* p : parts) {
        const auto b = geometry::bounds(domain::finishedContour(*p));
        // Shift on a 1e-6 mm grid: arc extremes carry float noise (min x = -7e-15 for a rounded
        // corner at x = 0) that must not leak into every coordinate.
        const double shift = std::round((cursor - b.min.x) * 1e6) / 1e6;
        if (auto r = drawPart(w, *p, model, options, {shift, 0}, out.diagnostics); !r) return r.error();
        cursor += (b.max.x - b.min.x) + options.spacing;
    }
    out.files.push_back({"", "", w.document()});
    return out;
}

} // namespace fos::formats
