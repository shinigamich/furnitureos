// SPDX-License-Identifier: AGPL-3.0-only
// SVG outline import: XML tree -> viewport scale -> shapes -> pieces -> chains -> loops.
#include "fos/formats/svg_import.hpp"

#include "fos/formats/container.hpp"
#include "svg_geometry.hpp"
#include "svg_xml.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <utility>

namespace fos::formats {
namespace {

using dxf::Chain;
using dxf::num;
using svg::Affine;
using svg::PieceEmitter;
using svg::XmlElement;

constexpr double kMaxCoordinateMm = 1e8;
constexpr double kPxMm = 25.4 / 96.0;

core::Error lineError(core::ErrorCode code, std::size_t line, const std::string& message) {
    return core::makeError(code, "SVG line " + std::to_string(line) + ": " + message);
}
core::Error lineError(std::size_t line, const core::Error& e) { return lineError(e.code, line, e.message); }

std::string describe(const XmlElement& el) {
    if (const auto* id = el.attribute("id")) return "<" + el.name + " id=\"" + *id + "\">";
    return "<" + el.name + ">";
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

/// A number with an optional unit suffix.
struct Length {
    double value = 0;
    std::string unit;
};

core::Result<Length> parseLength(std::string_view text) {
    text = trim(text);
    std::size_t i = 0;
    Length l;
    const auto st = svg::scanNumber(text, i, l.value);
    if (st == svg::NumberScan::OutOfRange)
        return core::makeError(core::ErrorCode::ParseError, "number '" + std::string(text) + "' is out of range.");
    if (st != svg::NumberScan::Ok)
        return core::makeError(core::ErrorCode::ParseError, "'" + std::string(text) + "' is not a number.");
    l.unit = std::string(trim(text.substr(i)));
    return l;
}

/// A shape attribute in user units (plain number or "px"); `fallback` when absent.
core::Result<double> userLength(const XmlElement& el, std::string_view name, double fallback) {
    const auto* v = el.attribute(name);
    if (!v) return fallback;
    auto l = parseLength(*v);
    if (!l) return lineError(el.line, core::makeError(l.error().code, describe(el) + " attribute " + std::string(name) + ": " + l.error().message));
    if (!l.value().unit.empty() && l.value().unit != "px")
        return lineError(core::ErrorCode::Unsupported, el.line,
                         describe(el) + " attribute " + std::string(name) + "=\"" + *v +
                             "\": units on shape attributes are not supported (only plain numbers or px).");
    return l.value().value;
}

std::optional<double> mmPerUnitName(const std::string& u) {
    if (u.empty() || u == "px") return kPxMm;
    if (u == "mm") return 1.0;
    if (u == "cm") return 10.0;
    if (u == "in") return 25.4;
    if (u == "pt") return 25.4 / 72.0;
    if (u == "pc") return 25.4 / 6.0;
    return std::nullopt;
}

/// Size of the root width / height in mm, nullopt when absent.
core::Result<std::optional<double>> rootSizeMm(const XmlElement& root, std::string_view name) {
    const auto* v = root.attribute(name);
    if (!v || trim(*v).empty()) return std::optional<double>{};
    auto l = parseLength(*v);
    if (!l) return lineError(root.line, core::makeError(l.error().code, "<svg> " + std::string(name) + ": " + l.error().message));
    const auto f = mmPerUnitName(l.value().unit);
    if (!f)
        return lineError(core::ErrorCode::Unsupported, root.line,
                         "<svg> " + std::string(name) + "=\"" + *v +
                             "\": the unit is not supported (mm, cm, in, pt, pc, px); give the scale with --scale.");
    if (!(l.value().value > 0))
        return lineError(core::ErrorCode::InvalidArgument, root.line, "<svg> " + std::string(name) + " must be > 0, got \"" + *v + "\".");
    return std::optional<double>{l.value().value * *f};
}

bool displayNone(const XmlElement& el) {
    if (const auto* d = el.attribute("display"); d && trim(*d) == "none") return true;
    if (const auto* s = el.attribute("style")) {
        std::string compact;
        for (char c : *s)
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') compact += c;
        const auto p = compact.find("display:none");
        if (p != std::string::npos && (p == 0 || compact[p - 1] == ';')) return true;
    }
    return false;
}

class Walker {
public:
    Walker(const SvgImportOptions& o, SvgImport& out) : opt_(o), out_(out) {}

    std::vector<Chain> closed, open;

    core::Result<void> walk(const XmlElement& el, const Affine& parent, bool isRoot) {
        if (displayNone(el)) {
            out_.diagnostics.push_back({core::Severity::Info, "formats.svg.hidden",
                                        describe(el) + " at line " + std::to_string(el.line) +
                                            " is hidden (display:none); its content is not imported.",
                                        {}});
            return {};
        }
        Affine m = parent;
        if (const auto* t = el.attribute("transform"); t) {
            auto tr = svg::parseTransform(*t);
            if (!tr) return lineError(el.line, core::makeError(tr.error().code, describe(el) + ": " + tr.error().message));
            m = parent * tr.value();
            if (!std::isfinite(m.det()) || std::fabs(m.det()) < 1e-18)
                return lineError(core::ErrorCode::InvalidArgument, el.line,
                                 describe(el) + ": transform \"" + *t + "\" is singular or out of range.");
        }
        static const std::set<std::string> containers{"g", "a", "switch"};
        static const std::set<std::string> silent{"defs", "symbol", "clipPath", "mask", "pattern", "marker",
                                                  "linearGradient", "radialGradient", "style", "title", "desc",
                                                  "metadata", "script", "filter", "namedview"};
        static const std::set<std::string> ignored{"text", "image", "use", "foreignObject", "svg", "tspan", "textPath"};
        const std::string& n = el.name;
        if (isRoot || containers.count(n)) {
            for (const auto& c : el.children)
                if (auto r = walk(c, m, false); !r) return r;
            return {};
        }
        if (n == "path" || n == "rect" || n == "circle" || n == "ellipse" || n == "line" || n == "polyline" ||
            n == "polygon") {
            ++out_.shapeCount;
            return shape(el, m);
        }
        if (ignored.count(n)) {
            std::string why = n == "use" ? " (references are not followed)"
                              : n == "image" ? " (external content is never fetched)"
                              : n == "svg" ? " (nested viewports are not supported)"
                                           : "";
            out_.diagnostics.push_back({core::Severity::Warning, "formats.svg.element_ignored",
                                        describe(el) + " at line " + std::to_string(el.line) + " is not imported" + why + ".",
                                        {}});
            return {};
        }
        if (silent.count(n) || el.qname.find(':') != std::string::npos) return {};
        out_.diagnostics.push_back({core::Severity::Warning, "formats.svg.element_ignored",
                                    "Unknown element " + describe(el) + " at line " + std::to_string(el.line) +
                                        " and its content are not imported.",
                                    {}});
        return {};
    }

private:
    core::Result<void> add(const XmlElement& el, std::vector<dxf::Piece> pieces, bool isClosed) {
        if (pieces.empty()) return {};
        pieceTotal_ += pieces.size();
        if (pieceTotal_ > opt_.maxPieces)
            return lineError(core::ErrorCode::InvalidArgument, el.line,
                             "more than " + std::to_string(opt_.maxPieces) + " segments in the file.");
        Chain c{std::move(pieces), el.line, describe(el)};
        (isClosed ? closed : open).push_back(std::move(c));
        return {};
    }

    core::Result<std::vector<double>> points(const XmlElement& el) {
        const auto* p = el.attribute("points");
        if (!p) return std::vector<double>{};
        auto v = svg::parseNumberList(*p, describe(el) + " points");
        if (!v) return lineError(el.line, v.error());
        if (v.value().size() % 2 != 0)
            return lineError(core::ErrorCode::ParseError, el.line,
                             describe(el) + " points has an odd count of numbers (" + std::to_string(v.value().size()) + ").");
        return v;
    }

    core::Result<void> shape(const XmlElement& el, const Affine& m) {
        const PieceEmitter emit(m, kMaxCoordinateMm);
        const auto pt = [&](double x, double y) -> core::Result<geometry::Vec2> {
            auto r = emit.point({x, y});
            if (!r) return lineError(el.line, core::makeError(r.error().code, describe(el) + ": " + r.error().message));
            return r;
        };
        const auto skip = [&](const std::string& why) -> core::Result<void> {
            out_.diagnostics.push_back({core::Severity::Warning, "formats.svg.shape_skipped",
                                        describe(el) + " at line " + std::to_string(el.line) + " " + why + "; not imported.", {}});
            return {};
        };
        const std::string& n = el.name;
        std::vector<dxf::Piece> pieces;
        if (n == "path") {
            const auto* d = el.attribute("d");
            if (!d || trim(*d).empty()) return skip("has no path data");
            auto subs = svg::parsePathData(*d, emit, opt_.maxPieces);
            if (!subs) return lineError(el.line, core::makeError(subs.error().code, describe(el) + ": " + subs.error().message));
            for (auto& s : subs.value())
                if (auto r = add(el, std::move(s.pieces), s.closed); !r) return r;
            return {};
        }
        if (n == "line") {
            double v[4];
            const char* names[4] = {"x1", "y1", "x2", "y2"};
            for (int k = 0; k < 4; ++k) {
                auto r = userLength(el, names[k], 0);
                if (!r) return r.error();
                v[k] = r.value();
            }
            auto a = pt(v[0], v[1]), b = pt(v[2], v[3]);
            if (!a) return a.error();
            if (!b) return b.error();
            emit.line(a.value(), b.value(), pieces);
            return add(el, std::move(pieces), false);
        }
        if (n == "polyline" || n == "polygon") {
            auto v = points(el);
            if (!v) return v.error();
            const auto& p = v.value();
            if (p.size() < 4) return skip("has fewer than 2 points");
            std::vector<geometry::Vec2> f;
            for (std::size_t k = 0; k < p.size(); k += 2) {
                auto q = pt(p[k], p[k + 1]);
                if (!q) return q.error();
                f.push_back(q.value());
            }
            for (std::size_t k = 0; k + 1 < f.size(); ++k) emit.line(f[k], f[k + 1], pieces);
            const bool isClosed = n == "polygon";
            if (isClosed) emit.line(f.back(), f.front(), pieces);
            return add(el, std::move(pieces), isClosed);
        }
        if (n == "circle" || n == "ellipse") {
            auto cx = userLength(el, "cx", 0), cy = userLength(el, "cy", 0);
            if (!cx) return cx.error();
            if (!cy) return cy.error();
            double rx = 0, ry = 0;
            if (n == "circle") {
                auto r = userLength(el, "r", 0);
                if (!r) return r.error();
                rx = ry = r.value();
            } else {
                auto a = userLength(el, "rx", -1), b = userLength(el, "ry", -1);
                if (!a) return a.error();
                if (!b) return b.error();
                rx = a.value() < 0 ? b.value() : a.value();
                ry = b.value() < 0 ? a.value() : b.value();
            }
            if (rx < 0 || ry < 0)
                return lineError(core::ErrorCode::InvalidArgument, el.line, describe(el) + " has a negative radius.");
            if (rx == 0 || ry == 0) return skip("has a zero radius");
            double radius = 0;
            if (!emit.circular(rx, ry, 0, radius))
                return lineError(core::ErrorCode::Unsupported, el.line,
                                 describe(el) + " with radii " + num(rx) + " and " + num(ry) +
                                     " is not a circle after its transform; only circular outlines are imported.");
            auto a = pt(cx.value() + rx, cy.value()), b = pt(cx.value() - rx, cy.value());
            if (!a) return a.error();
            if (!b) return b.error();
            const double bulge = emit.det() < 0 ? -1.0 : 1.0;
            pieces.push_back({a.value(), b.value(), bulge});
            pieces.push_back({b.value(), a.value(), bulge});
            return add(el, std::move(pieces), true);
        }
        // rect
        double v[6];
        const char* names[6] = {"x", "y", "width", "height", "rx", "ry"};
        for (int k = 0; k < 6; ++k) {
            auto r = userLength(el, names[k], k >= 4 ? -1.0 : 0.0);
            if (!r) return r.error();
            v[k] = r.value();
        }
        const double x = v[0], y = v[1], w = v[2], h = v[3];
        if (w < 0 || h < 0)
            return lineError(core::ErrorCode::InvalidArgument, el.line, describe(el) + " has a negative width or height.");
        if (w == 0 || h == 0) return skip("has a zero width or height");
        if ((el.attribute("rx") && v[4] < 0) || (el.attribute("ry") && v[5] < 0))
            return lineError(core::ErrorCode::InvalidArgument, el.line, describe(el) + " has a negative corner radius.");
        double rx = el.attribute("rx") ? v[4] : (el.attribute("ry") ? v[5] : 0.0);
        double ry = el.attribute("ry") ? v[5] : rx;
        rx = std::min(rx, w / 2);
        ry = std::min(ry, h / 2);
        if (rx == 0 || ry == 0) rx = ry = 0; // SVG: a zero radius on either axis means square corners
        std::vector<geometry::Vec2> corners;
        const double pts[8][2] = {{x + rx, y},     {x + w - rx, y},     {x + w, y + ry}, {x + w, y + h - ry},
                                  {x + w - rx, y + h}, {x + rx, y + h}, {x, y + h - ry}, {x, y + ry}};
        for (const auto& p : pts) {
            auto q = pt(p[0], p[1]);
            if (!q) return q.error();
            corners.push_back(q.value());
        }
        if (rx > 0 && ry > 0) {
            double radius = 0;
            if (!emit.circular(rx, ry, 0, radius))
                return lineError(core::ErrorCode::Unsupported, el.line,
                                 describe(el) + " corner radii rx " + num(rx) + " and ry " + num(ry) +
                                     " are not circular after its transform; only circular corners are imported.");
            // Going +x then +y is counter-clockwise in user space: each corner is a +90 degree arc.
            const double b = (emit.det() < 0 ? -1.0 : 1.0) * std::tan(std::numbers::pi / 8);
            for (std::size_t k = 0; k < 8; k += 2) {
                emit.line(corners[k], corners[k + 1], pieces);
                pieces.push_back({corners[k + 1], corners[(k + 2) % 8], b});
            }
        } else {
            for (std::size_t k = 1; k < 8; k += 2) emit.line(corners[k], corners[(k + 2) % 8], pieces);
        }
        return add(el, std::move(pieces), true);
    }

    const SvgImportOptions& opt_;
    SvgImport& out_;
    std::size_t pieceTotal_ = 0;
};

/// The DXF chain helpers speak of "line N (layer 'X')" with codes "formats.dxf.*"; in SVG terms
/// the "layer" is the element.
void renameChainDiagnostics(core::Diagnostics& ds, std::size_t from) {
    for (std::size_t k = from; k < ds.size(); ++k) {
        auto& d = ds[k];
        if (d.code.rfind("formats.dxf.", 0) == 0) d.code = "formats.svg." + d.code.substr(12);
        for (auto p = d.message.find("(layer '"); p != std::string::npos; p = d.message.find("(layer '", p))
            d.message.replace(p, 8, "(element '");
    }
}

} // namespace

core::Result<SvgImport> importSvg(std::string_view text, const SvgImportOptions& o) {
    if (o.scale && !(*o.scale > 0 && *o.scale <= 1e6))
        return core::makeError(core::ErrorCode::InvalidArgument, "The SVG scale must be in (0, 1e6] mm per unit, got " + num(*o.scale) + ".");
    if (!(o.chainTolerance > 0 && o.chainTolerance <= 10))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "The chain tolerance must be in (0, 10] mm, got " + num(o.chainTolerance) + ".");
    if (text.size() > o.maxBytes)
        return core::makeError(core::ErrorCode::InvalidArgument, "The SVG text is " + std::to_string(text.size()) +
                                                                     " bytes, more than the limit of " + std::to_string(o.maxBytes) + ".");
    SvgImport out;
    auto tree = svg::parseXml(text, svg::XmlLimits{o.maxElements, o.maxDepth, 256}, out.elementCount);
    if (!tree) return tree.error();
    const XmlElement& root = tree.value();
    if (root.name != "svg")
        return lineError(core::ErrorCode::InvalidArgument, root.line, "the root element is <" + root.qname + ">, not <svg>.");

    // Viewport: user unit -> mm.
    double sx = kPxMm, sy = kPxMm, tx = 0, ty = 0;
    if (o.scale) {
        sx = sy = *o.scale;
        out.unitSource = "--scale " + num(*o.scale) + " mm per unit";
    } else {
        auto w = rootSizeMm(root, "width");
        if (!w) return w.error();
        auto h = rootSizeMm(root, "height");
        if (!h) return h.error();
        const auto* vb = root.attribute("viewBox");
        if (vb) {
            auto v = svg::parseNumberList(*vb, "<svg> viewBox");
            if (!v) return lineError(root.line, v.error());
            if (v.value().size() != 4 || !(v.value()[2] > 0) || !(v.value()[3] > 0))
                return lineError(core::ErrorCode::InvalidArgument, root.line,
                                 "<svg> viewBox=\"" + *vb + "\" must be four numbers with width and height > 0.");
            const auto& b = v.value();
            tx = -b[0];
            ty = -b[1];
            if (w.value() && h.value()) {
                sx = *w.value() / b[2];
                sy = *h.value() / b[3];
                const auto* par = root.attribute("preserveAspectRatio");
                const bool none = par && par->find("none") != std::string::npos;
                if (!none) sx = sy = (par && par->find("slice") != std::string::npos) ? std::max(sx, sy) : std::min(sx, sy);
                out.unitSource = "width/height " + num(*w.value()) + " x " + num(*h.value()) + " mm over viewBox " +
                                 num(b[2]) + " x " + num(b[3]);
            } else if (w.value() || h.value()) {
                sx = sy = w.value() ? *w.value() / b[2] : *h.value() / b[3];
                out.unitSource = std::string(w.value() ? "width " : "height ") + num(w.value() ? *w.value() : *h.value()) +
                                 " mm over the viewBox";
            } else {
                out.unitSource = "px (96 per inch; no width/height)";
                out.diagnostics.push_back({core::Severity::Warning, "formats.svg.unitless",
                                           "The SVG has a viewBox but no width/height; user units are read as px (1/96 in = "
                                           "0.2646 mm). Give --scale if that is wrong.",
                                           {}});
            }
        } else {
            out.unitSource = "px (96 per inch; no viewBox)";
            if (!w.value() && !h.value())
                out.diagnostics.push_back({core::Severity::Warning, "formats.svg.unitless",
                                           "The SVG has no width/height or viewBox; coordinates are read as px (1/96 in = "
                                           "0.2646 mm). Give --scale if that is wrong.",
                                           {}});
        }
    }
    out.mmPerUnitX = sx;
    out.mmPerUnitY = sy;
    // y is flipped so the drawing's top is the panel's +y.
    const Affine rootM = Affine{sx, 0, 0, -sy, 0, 0} * Affine{1, 0, 0, 1, tx, ty};

    Walker walker(o, out);
    if (auto r = walker.walk(root, rootM, true); !r) return r.error();

    const std::size_t diagStart = out.diagnostics.size();
    std::vector<Chain> closed = std::move(walker.closed);
    if (!walker.open.empty()) dxf::chainPieces(std::move(walker.open), o.chainTolerance, closed, out.diagnostics);
    for (const auto& c : closed) {
        DxfLoop loop;
        if (dxf::makeLoop(c, o.maxLoopVertices, loop, out.diagnostics))
            out.loops.push_back({std::move(loop.contour), loop.line, loop.layer});
    }
    renameChainDiagnostics(out.diagnostics, diagStart);
    std::stable_sort(out.loops.begin(), out.loops.end(), [](const SvgLoop& a, const SvgLoop& b) { return a.line < b.line; });
    return out;
}

core::Result<SvgImport> importSvgFile(const std::filesystem::path& path, const SvgImportOptions& options) {
    auto text = readFile(path, ReadLimits{options.maxBytes});
    if (!text) return text.error();
    return importSvg(text.value(), options);
}

std::vector<geometry::Contour> contoursOf(const SvgImport& import) {
    std::vector<geometry::Contour> v;
    v.reserve(import.loops.size());
    for (const auto& l : import.loops) v.push_back(l.contour);
    return v;
}

} // namespace fos::formats
