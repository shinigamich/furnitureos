// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/drawings/svg.hpp"

#include "format.hpp"

#include <cmath>

namespace fos::drawings {

using detail::num;

std::string xmlEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n' && c != '\r')
                out += ' ';
            else
                out += c;
        }
    }
    return out;
}

namespace {

struct Writer {
    const Sheet& sheet;
    const SvgOptions& opt;
    std::string out;

    double fy(double y) const noexcept { return sheet.height - y; }
    std::string xy(Vec2 p) const { return num(p.x) + "," + num(fy(p.y)); }

    template <class P> std::string meta(const P& p) const {
        std::string s;
        if (!opt.dataAttributes) return s;
        if (!p.sourceId.empty()) s += " data-source=\"" + xmlEscape(p.sourceId) + "\"";
        if (!p.elementId.empty()) s += " data-element=\"" + xmlEscape(p.elementId) + "\"";
        return s;
    }
    std::string stroke(LineStyle style, double weight) const {
        std::string s = " stroke-width=\"" + num(weight) + "\"";
        const std::vector<double> dash = dashPattern(style);
        if (!dash.empty()) {
            s += " stroke-dasharray=\"";
            for (std::size_t i = 0; i < dash.size(); ++i) s += (i ? " " : "") + num(dash[i]);
            s += "\"";
        }
        return s;
    }

    void operator()(const Line& l) {
        out += "<line x1=\"" + num(l.a.x) + "\" y1=\"" + num(fy(l.a.y)) + "\" x2=\"" + num(l.b.x) + "\" y2=\"" +
               num(fy(l.b.y)) + "\"" + stroke(l.style, l.weight) + meta(l) + "/>\n";
    }
    void operator()(const Polyline& p) {
        if (p.points.size() < 2) return;
        out += p.closed ? "<polygon points=\"" : "<polyline points=\"";
        for (std::size_t i = 0; i < p.points.size(); ++i) out += (i ? " " : "") + xy(p.points[i]);
        out += "\"" + stroke(p.style, p.weight) + meta(p) + "/>\n";
    }
    void operator()(const Circle& c) {
        out += "<circle cx=\"" + num(c.center.x) + "\" cy=\"" + num(fy(c.center.y)) + "\" r=\"" + num(c.radius) +
               "\"" + stroke(c.style, c.weight) + meta(c) + "/>\n";
    }
    void operator()(const Arc& a) {
        double sweep = std::fmod(a.endDeg - a.startDeg, 360.0);
        if (sweep <= 0) sweep += 360.0;
        const auto at = [&](double deg) {
            const double t = core::degToRad(deg);
            return a.center + Vec2{std::cos(t), std::sin(t)} * a.radius;
        };
        const std::string r = num(a.radius);
        if (sweep >= 360.0 - 1e-9) { // full turn: two half arcs
            out += "<path d=\"M " + xy(at(a.startDeg)) + " A " + r + " " + r + " 0 1 0 " + xy(at(a.startDeg + 180.0)) +
                   " A " + r + " " + r + " 0 1 0 " + xy(at(a.startDeg)) + "\"" + stroke(a.style, a.weight) + meta(a) +
                   "/>\n";
            return;
        }
        // Counter-clockwise on paper = sweep-flag 0 after the Y flip.
        out += "<path d=\"M " + xy(at(a.startDeg)) + " A " + r + " " + r + " 0 " + (sweep > 180.0 ? "1" : "0") +
               " 0 " + xy(at(a.startDeg + sweep)) + "\"" + stroke(a.style, a.weight) + meta(a) + "/>\n";
    }
    void operator()(const Text& t) {
        // Shift the anchor to the baseline along the text's local "down" direction.
        double down = 0.0;
        if (t.vAlign == VAlign::Middle) down = 0.35 * t.height;
        if (t.vAlign == VAlign::Top) down = 0.7 * t.height;
        const double a = core::degToRad(t.rotationDeg);
        const Vec2 p = t.position + Vec2{std::sin(a), -std::cos(a)} * down;
        const char* anchor = t.hAlign == HAlign::Left ? "start" : t.hAlign == HAlign::Center ? "middle" : "end";
        out += "<text x=\"" + num(p.x) + "\" y=\"" + num(fy(p.y)) + "\" font-size=\"" + num(t.height) +
               "\" text-anchor=\"" + anchor + "\"";
        if (std::fabs(t.rotationDeg) > 1e-9)
            out += " transform=\"rotate(" + num(-t.rotationDeg) + " " + num(p.x) + " " + num(fy(p.y)) + ")\"";
        out += " fill=\"" + xmlEscape(opt.strokeColor) + "\" stroke=\"none\"" + meta(t) + ">" + xmlEscape(t.text) +
               "</text>\n";
    }
    void operator()(const Marker& m) {
        const std::string fill = " fill=\"" + xmlEscape(opt.strokeColor) + "\" stroke=\"none\"";
        const std::vector<Vec2> pts = markerOutline(m);
        switch (m.kind) {
        case MarkerKind::Arrow:
            out += "<polygon points=\"" + xy(pts[0]) + " " + xy(pts[1]) + " " + xy(pts[2]) + "\"" + fill + meta(m) +
                   "/>\n";
            break;
        case MarkerKind::Tick:
            out += "<line x1=\"" + num(pts[0].x) + "\" y1=\"" + num(fy(pts[0].y)) + "\" x2=\"" + num(pts[1].x) +
                   "\" y2=\"" + num(fy(pts[1].y)) + "\" stroke-width=\"0.35\"" + meta(m) + "/>\n";
            break;
        case MarkerKind::Dot:
            out += "<circle cx=\"" + num(m.position.x) + "\" cy=\"" + num(fy(m.position.y)) + "\" r=\"" +
                   num(m.size * 0.5) + "\"" + fill + meta(m) + "/>\n";
            break;
        }
    }
};

} // namespace

std::string toSvg(const Sheet& sheet, const SvgOptions& options) {
    Writer w{sheet, options, {}};
    const std::string wmm = num(sheet.width), hmm = num(sheet.height);
    w.out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    w.out += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"" + wmm + "mm\" height=\"" + hmm +
             "mm\" viewBox=\"0 0 " + wmm + " " + hmm + "\">\n";
    if (!options.background.empty())
        w.out += "<rect x=\"0\" y=\"0\" width=\"" + wmm + "\" height=\"" + hmm + "\" fill=\"" +
                 xmlEscape(options.background) + "\"/>\n";
    w.out += "<g fill=\"none\" stroke=\"" + xmlEscape(options.strokeColor) +
             "\" stroke-linecap=\"butt\" stroke-linejoin=\"miter\" font-family=\"" + xmlEscape(options.fontFamily) +
             "\">\n";
    for (const Primitive& p : sheet.primitives) std::visit(w, p);
    w.out += "</g>\n</svg>\n";
    return w.out;
}

} // namespace fos::drawings
