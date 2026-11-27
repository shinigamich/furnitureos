// SPDX-License-Identifier: AGPL-3.0-only
// SVG numbers, transforms and path data -> pieces (straight lines and circular arcs as bulges).
#include "svg_geometry.hpp"

#include <charconv>
#include <cmath>
#include <numbers>

namespace fos::formats::svg {
namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kMinPiece = 1e-9; // mm

core::Error unsupported(const std::string& m) { return core::makeError(core::ErrorCode::Unsupported, m); }
core::Error bad(const std::string& m) { return core::makeError(core::ErrorCode::ParseError, m); }

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

std::string near(std::string_view s, std::size_t i) {
    return "at offset " + std::to_string(i) + " ('" + std::string(s.substr(i, 12)) + "')";
}

} // namespace

Affine operator*(const Affine& l, const Affine& r) {
    return {l.a * r.a + l.c * r.b, l.b * r.a + l.d * r.b, l.a * r.c + l.c * r.d,
            l.b * r.c + l.d * r.d, l.a * r.e + l.c * r.f + l.e, l.b * r.e + l.d * r.f + l.f};
}

void cosSinDeg(double deg, double& c, double& s) {
    const double q = deg / 90.0;
    if (std::fabs(q) < 1e15 && q == std::floor(q)) {
        const long long k = ((static_cast<long long>(q) % 4) + 4) % 4;
        static constexpr double cs[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        c = cs[k][0];
        s = cs[k][1];
        return;
    }
    c = std::cos(deg * kPi / 180.0);
    s = std::sin(deg * kPi / 180.0);
}

NumberScan scanNumber(std::string_view s, std::size_t& i, double& out) {
    std::size_t j = i;
    bool negative = false;
    if (j < s.size() && (s[j] == '+' || s[j] == '-')) negative = s[j++] == '-';
    const std::size_t start = j;
    std::size_t digits = 0;
    while (j < s.size() && isDigit(s[j])) ++j, ++digits;
    if (j < s.size() && s[j] == '.') {
        ++j;
        while (j < s.size() && isDigit(s[j])) ++j, ++digits;
    }
    if (digits == 0) return NumberScan::None;
    if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) {
        std::size_t k = j + 1;
        if (k < s.size() && (s[k] == '+' || s[k] == '-')) ++k;
        const std::size_t ed = k;
        while (k < s.size() && isDigit(s[k])) ++k;
        if (k > ed) j = k;
    }
    double v = 0;
    const auto r = std::from_chars(s.data() + start, s.data() + j, v, std::chars_format::general);
    if (r.ec == std::errc::result_out_of_range) return NumberScan::OutOfRange;
    if (r.ec != std::errc() || r.ptr != s.data() + j) return NumberScan::None;
    if (!std::isfinite(v)) return NumberScan::OutOfRange;
    out = negative ? -v : v;
    i = j;
    return NumberScan::Ok;
}

void skipSeparators(std::string_view s, std::size_t& i) {
    while (i < s.size() && isSpace(s[i])) ++i;
    if (i < s.size() && s[i] == ',') {
        ++i;
        while (i < s.size() && isSpace(s[i])) ++i;
    }
}

core::Result<std::vector<double>> parseNumberList(std::string_view s, std::string_view what) {
    std::vector<double> v;
    std::size_t i = 0;
    while (i < s.size() && isSpace(s[i])) ++i;
    while (i < s.size()) {
        double x = 0;
        const auto st = scanNumber(s, i, x);
        if (st == NumberScan::OutOfRange) return bad(std::string(what) + ": number out of range " + near(s, i) + ".");
        if (st != NumberScan::Ok) return bad(std::string(what) + ": expected a number " + near(s, i) + ".");
        v.push_back(x);
        skipSeparators(s, i);
    }
    return v;
}

core::Result<Affine> parseTransform(std::string_view s) {
    Affine m;
    std::size_t i = 0;
    while (true) {
        while (i < s.size() && (isSpace(s[i]) || s[i] == ',')) ++i;
        if (i >= s.size()) break;
        const std::size_t ns = i;
        while (i < s.size() && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z'))) ++i;
        const std::string name(s.substr(ns, i - ns));
        while (i < s.size() && isSpace(s[i])) ++i;
        if (name.empty() || i >= s.size() || s[i] != '(')
            return bad("transform '" + std::string(s) + "': expected a function such as translate(...) " + near(s, ns) + ".");
        const auto close = s.find(')', i);
        if (close == std::string_view::npos) return bad("transform '" + std::string(s) + "': missing ')'.");
        if (name == "skewX" || name == "skewY")
            return unsupported("transform " + name + "(...) is not supported (skewed outlines are refused); remove the "
                               "skew in the drawing program.");
        auto args = parseNumberList(s.substr(i + 1, close - i - 1), "transform " + name);
        if (!args) return args.error();
        const auto& a = args.value();
        Affine t;
        const auto count = [&](std::size_t lo, std::size_t hi) { return a.size() >= lo && a.size() <= hi; };
        if (name == "matrix" && a.size() == 6) {
            t = {a[0], a[1], a[2], a[3], a[4], a[5]};
        } else if (name == "translate" && count(1, 2)) {
            t.e = a[0];
            t.f = a.size() == 2 ? a[1] : 0.0;
        } else if (name == "scale" && count(1, 2)) {
            t.a = a[0];
            t.d = a.size() == 2 ? a[1] : a[0];
        } else if (name == "rotate" && (a.size() == 1 || a.size() == 3)) {
            double c = 1, sn = 0;
            cosSinDeg(a[0], c, sn);
            t = {c, sn, -sn, c, 0, 0};
            if (a.size() == 3) t = Affine{1, 0, 0, 1, a[1], a[2]} * t * Affine{1, 0, 0, 1, -a[1], -a[2]};
        } else if (name == "matrix" || name == "translate" || name == "scale" || name == "rotate") {
            return bad("transform " + name + "(...) has " + std::to_string(a.size()) + " argument(s), which is not valid.");
        } else {
            return unsupported("transform function '" + name + "' is not supported.");
        }
        m = m * t;
        i = close + 1;
    }
    return m;
}

core::Result<Vec2> PieceEmitter::point(Vec2 p) const {
    const Vec2 q = m_.apply(p);
    if (!std::isfinite(q.x) || !std::isfinite(q.y) || std::fabs(q.x) > max_ || std::fabs(q.y) > max_)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "point (" + dxf::num(p.x) + ", " + dxf::num(p.y) + ") lies beyond the coordinate limit of " +
                                   dxf::num(max_) + " mm after scaling.");
    return q;
}

void PieceEmitter::line(Vec2 a, Vec2 b, std::vector<Piece>& out) const {
    if (geometry::length(b - a) >= kMinPiece) out.push_back({a, b, 0.0});
}

bool PieceEmitter::circular(double rx, double ry, double rotationDeg, double& radius) const {
    double c = 1, s = 0;
    cosSinDeg(rotationDeg, c, s);
    const Vec2 u = m_.linear({rx * c, rx * s}), v = m_.linear({-ry * s, ry * c});
    const double lu = geometry::length(u), lv = geometry::length(v);
    if (!std::isfinite(lu) || !std::isfinite(lv) || lu <= 0 || lv <= 0) return false;
    const double tol = 1e-6;
    if (std::fabs(lu - lv) > tol * std::max(lu, lv) || std::fabs(geometry::dot(u, v)) > tol * lu * lv) return false;
    radius = (lu + lv) / 2;
    return true;
}

core::Result<void> PieceEmitter::arc(Vec2 p, double rx, double ry, double rotationDeg, bool largeArc, bool sweep,
                                     Vec2 q, std::vector<Piece>& out) const {
    const double d = geometry::length(q - p);
    if (d < kMinPiece) return {}; // SVG: an arc to its own start point is omitted
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    if (rx == 0 || ry == 0) { // SVG: a zero radius makes the arc a straight line
        line(p, q, out);
        return {};
    }
    double r = 0;
    if (!circular(rx, ry, rotationDeg, r))
        return unsupported("elliptical arc (rx " + dxf::num(rx) + ", ry " + dxf::num(ry) +
                           ") is not a circular arc after its transform; only circular arcs are imported.");
    if (!std::isfinite(r) || r > max_) return core::makeError(core::ErrorCode::InvalidArgument, "arc radius beyond the coordinate limit.");
    r = std::max(r, d / 2); // SVG out-of-range radii are scaled up until the arc fits
    const bool ccw = sweep != (m_.det() < 0);
    const double sign = ccw ? 1.0 : -1.0;
    const double small = 2 * std::asin(std::min(1.0, d / (2 * r)));
    const double theta = largeArc ? 2 * kPi - small : small;
    if (theta <= kPi * (1 + 1e-12)) {
        out.push_back({p, q, sign * std::min(1.0, std::tan(theta / 4))});
        return {};
    }
    const Vec2 mid = (p + q) * 0.5;
    const Vec2 n{-(q.y - p.y) / d, (q.x - p.x) / d};
    const double h = std::sqrt(std::max(0.0, r * r - d * d / 4));
    const Vec2 apex = mid - n * (sign * (r + h));
    const double half = sign * std::tan(theta / 8);
    out.push_back({p, apex, half});
    out.push_back({apex, q, half});
    return {};
}

core::Result<std::vector<SubPath>> parsePathData(std::string_view s, const PieceEmitter& emit, std::size_t maxPieces) {
    std::vector<SubPath> paths;
    SubPath cur;
    Vec2 pos{0, 0}, start{0, 0}; // local
    Vec2 posF{0, 0}, startF{0, 0}; // final
    bool havePoint = false;
    char cmd = 0;
    std::size_t total = 0;
    std::size_t i = 0;

    const auto finish = [&] {
        total += cur.pieces.size();
        if (!cur.pieces.empty()) paths.push_back(std::move(cur));
        cur = SubPath{};
    };
    const auto number = [&](double& v) -> core::Result<void> {
        skipSeparators(s, i);
        const std::size_t at = i;
        const auto st = scanNumber(s, i, v);
        if (st == NumberScan::OutOfRange) return bad("path data: number out of range " + near(s, at) + ".");
        if (st != NumberScan::Ok)
            return bad(std::string("path data: command '") + cmd + "' expects a number " + near(s, at) + ".");
        return {};
    };
    const auto flag = [&](bool& f) -> core::Result<void> {
        skipSeparators(s, i);
        if (i < s.size() && (s[i] == '0' || s[i] == '1')) {
            f = s[i++] == '1';
            return {};
        }
        return bad("path data: arc flag must be 0 or 1 " + near(s, i) + ".");
    };
    const auto moveTo = [&](Vec2 local) -> core::Result<void> {
        auto f = emit.point(local);
        if (!f) return f.error();
        pos = local;
        posF = f.value();
        return {};
    };

    while (true) {
        while (i < s.size() && (isSpace(s[i]) || s[i] == ',')) ++i;
        if (i >= s.size()) break;
        const char c = s[i];
        const bool isLetter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (isLetter) {
            cmd = c;
            ++i;
        } else if (cmd == 0) {
            return bad("path data must start with a moveto command (M or m) " + near(s, i) + ".");
        } else if (cmd == 'Z' || cmd == 'z') {
            return bad("path data: a number after 'Z' " + near(s, i) + ".");
        } else if (cmd == 'M') {
            cmd = 'L';
        } else if (cmd == 'm') {
            cmd = 'l';
        }
        const bool rel = cmd >= 'a' && cmd <= 'z';
        const char up = static_cast<char>(rel ? cmd - 'a' + 'A' : cmd);
        if (up != 'M' && !havePoint && up != 'Z')
            return bad("path data must start with a moveto command (M or m) " + near(s, i) + ".");
        const Vec2 base = rel ? pos : Vec2{0, 0};
        switch (up) {
        case 'M': {
            double x = 0, y = 0;
            if (auto r = number(x); !r) return r.error();
            if (auto r = number(y); !r) return r.error();
            finish();
            if (auto r = moveTo(Vec2{x, y} + (havePoint ? base : Vec2{0, 0})); !r) return r.error();
            start = pos;
            startF = posF;
            havePoint = true;
            break;
        }
        case 'L':
        case 'H':
        case 'V': {
            Vec2 to = pos;
            double x = 0, y = 0;
            if (up == 'L') {
                if (auto r = number(x); !r) return r.error();
                if (auto r = number(y); !r) return r.error();
                to = Vec2{x, y} + base;
            } else if (up == 'H') {
                if (auto r = number(x); !r) return r.error();
                to.x = rel ? pos.x + x : x;
            } else {
                if (auto r = number(y); !r) return r.error();
                to.y = rel ? pos.y + y : y;
            }
            const Vec2 from = posF;
            if (auto r = moveTo(to); !r) return r.error();
            emit.line(from, posF, cur.pieces);
            break;
        }
        case 'A': {
            double rx = 0, ry = 0, rot = 0, x = 0, y = 0;
            bool large = false, sweep = false;
            if (auto r = number(rx); !r) return r.error();
            if (auto r = number(ry); !r) return r.error();
            if (auto r = number(rot); !r) return r.error();
            if (auto r = flag(large); !r) return r.error();
            if (auto r = flag(sweep); !r) return r.error();
            if (auto r = number(x); !r) return r.error();
            if (auto r = number(y); !r) return r.error();
            const Vec2 from = posF;
            if (auto r = moveTo(Vec2{x, y} + base); !r) return r.error();
            if (auto r = emit.arc(from, rx, ry, rot, large, sweep, posF, cur.pieces); !r) return r.error();
            break;
        }
        case 'Z': {
            if (!havePoint) return bad("path data must start with a moveto command (M or m).");
            if (!cur.pieces.empty() && geometry::length(cur.pieces.back().b - startF) <= kMinPiece)
                cur.pieces.back().b = startF;
            else emit.line(posF, startF, cur.pieces);
            cur.closed = true;
            pos = start;
            posF = startF;
            finish();
            break;
        }
        case 'C':
        case 'S':
        case 'Q':
        case 'T':
            return unsupported(std::string("Bezier curve command '") + cmd + "' " + near(s, i - 1) +
                               " is not supported; convert curves to lines and circular arcs in the drawing program "
                               "(Inkscape: Extensions > Modify Path > Flatten Beziers) or use DXF.");
        default:
            return bad(std::string("path data: unknown command '") + cmd + "' " + near(s, i - 1) + ".");
        }
        if (total + cur.pieces.size() > maxPieces)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "path data has more than " + std::to_string(maxPieces) + " segments.");
    }
    finish();
    return paths;
}

} // namespace fos::formats::svg
