// SPDX-License-Identifier: AGPL-3.0-only
// ASCII DXF reader: group-code pairs -> sections -> supported entities -> pieces -> loops.
#include "dxf_internal.hpp"

#include "fos/formats/container.hpp"

#include <charconv>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <utility>

namespace fos::formats {

const char* toString(DxfUnit unit) noexcept {
    switch (unit) {
    case DxfUnit::Millimetre: return "mm";
    case DxfUnit::Centimetre: return "cm";
    case DxfUnit::Metre: return "m";
    case DxfUnit::Inch: return "in";
    case DxfUnit::Foot: return "ft";
    }
    return "mm";
}

std::optional<DxfUnit> dxfUnitFromString(std::string_view t) noexcept {
    if (t == "mm") return DxfUnit::Millimetre;
    if (t == "cm") return DxfUnit::Centimetre;
    if (t == "m") return DxfUnit::Metre;
    if (t == "in" || t == "inch") return DxfUnit::Inch;
    if (t == "ft") return DxfUnit::Foot;
    return std::nullopt;
}

double millimetresPer(DxfUnit unit) noexcept {
    switch (unit) {
    case DxfUnit::Millimetre: return 1.0;
    case DxfUnit::Centimetre: return 10.0;
    case DxfUnit::Metre: return 1000.0;
    case DxfUnit::Inch: return 25.4;
    case DxfUnit::Foot: return 304.8;
    }
    return 1.0;
}

namespace {

using dxf::Chain;
using dxf::num;
using dxf::Piece;
using geometry::Vec2;

constexpr double kMaxCoordinateMm = 1e8;
constexpr double kPi = std::numbers::pi;

struct Pair {
    int code = 0;
    std::string_view value;
    std::size_t line = 0; ///< Line of the group code.
};

core::Error parseError(std::size_t line, const std::string& message) {
    return core::makeError(core::ErrorCode::ParseError, "DXF line " + std::to_string(line) + ": " + message);
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

class Lexer {
public:
    explicit Lexer(std::string_view text) : text_(text) {
        if (text_.substr(0, 3) == "\xEF\xBB\xBF") text_.remove_prefix(3);
    }

    /// Next pair; nullopt at the end of the text. Errors for a malformed pair.
    core::Result<std::optional<Pair>> next() {
        if (peeked_) {
            auto p = *peeked_;
            peeked_.reset();
            return std::optional<Pair>(p);
        }
        std::string_view codeLine;
        if (!readLine(codeLine)) return std::optional<Pair>();
        const std::size_t codeLineNo = line_;
        const std::string_view codeText = trim(codeLine);
        if (codeText.empty() && atEnd()) return std::optional<Pair>(); // trailing blank line
        int code = 0;
        const auto [ptr, ec] = std::from_chars(codeText.data(), codeText.data() + codeText.size(), code);
        if (codeText.empty() || ec != std::errc() || ptr != codeText.data() + codeText.size() || code < 0)
            return parseError(codeLineNo, "expected an integer group code, found '" +
                                              std::string(codeText.substr(0, 40)) + "' (malformed DXF).");
        std::string_view value;
        if (!readLine(value))
            return parseError(codeLineNo, "group code " + std::to_string(code) +
                                              " has no value line (the file is truncated).");
        return std::optional<Pair>(Pair{code, trim(value), codeLineNo});
    }

    void pushBack(const Pair& p) { peeked_ = p; }
    std::size_t line() const noexcept { return line_; }

private:
    bool atEnd() const noexcept { return pos_ >= text_.size(); }
    bool readLine(std::string_view& out) {
        if (pos_ >= text_.size()) return false;
        const std::size_t nl = text_.find('\n', pos_);
        const std::size_t end = nl == std::string_view::npos ? text_.size() : nl;
        out = text_.substr(pos_, end - pos_);
        pos_ = nl == std::string_view::npos ? text_.size() : nl + 1;
        ++line_;
        return true;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    std::size_t line_ = 0;
    std::optional<Pair> peeked_;
};

core::Result<double> number(const Pair& p) {
    std::string_view t = p.value;
    if (!t.empty() && t.front() == '+') t.remove_prefix(1);
    double v = 0;
    const auto [ptr, ec] = std::from_chars(t.data(), t.data() + t.size(), v);
    if (t.empty() || ec != std::errc() || ptr != t.data() + t.size() || !std::isfinite(v))
        return parseError(p.line + 1, "group code " + std::to_string(p.code) + " needs a finite number, found '" +
                                          std::string(p.value.substr(0, 40)) + "'.");
    return v;
}

core::Result<long long> integer(const Pair& p) {
    auto v = number(p);
    if (!v) return v.error();
    if (std::fabs(v.value()) > 1e15 || v.value() != std::floor(v.value()))
        return parseError(p.line + 1, "group code " + std::to_string(p.code) + " needs an integer, found '" +
                                          std::string(p.value.substr(0, 40)) + "'.");
    return static_cast<long long>(v.value());
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

/// One entity: its type and every pair up to the next group code 0.
struct Entity {
    std::string type;
    std::size_t line = 0;
    std::vector<Pair> pairs;
    std::string layer() const {
        for (const auto& p : pairs)
            if (p.code == 8) return std::string(p.value);
        return "0";
    }
};

/// Splits an arc (centre, radius, start angle, signed sweep; radians) into pieces of at most
/// 180 degrees.
void appendArc(std::vector<Piece>& out, Vec2 c, double r, double a0, double sweep) {
    const int n = std::max(1, static_cast<int>(std::ceil(std::fabs(sweep) / kPi - 1e-12)));
    const double s = sweep / n;
    for (int k = 0; k < n; ++k) {
        const double t0 = a0 + s * k, t1 = a0 + s * (k + 1);
        out.push_back({{c.x + r * std::cos(t0), c.y + r * std::sin(t0)},
                       {c.x + r * std::cos(t1), c.y + r * std::sin(t1)},
                       std::tan(s / 4)});
    }
}

/// Appends the segment p -> q with polyline bulge b, split when |b| > 1.
void appendBulge(std::vector<Piece>& out, Vec2 p, Vec2 q, double b) {
    if (std::fabs(b) <= 1.0) {
        out.push_back({p, q, b});
        return;
    }
    const Vec2 d = q - p;
    const Vec2 mid{(p.x + q.x) / 2, (p.y + q.y) / 2};
    const Vec2 right{d.y / 2, -d.x / 2}; // half-chord length, perpendicular (right of p -> q)
    const Vec2 apex{mid.x + b * right.x, mid.y + b * right.y};
    const double half = b / (1.0 + std::sqrt(1.0 + b * b)); // tan(theta / 8) from tan(theta / 4)
    out.push_back({p, apex, half});
    out.push_back({apex, q, half});
}

struct Extrusion {
    bool supported = true;
    bool mirrored = false;
};

core::Result<Extrusion> extrusionOf(const Entity& e) {
    double x = 0, y = 0, z = 1;
    for (const auto& p : e.pairs) {
        if (p.code != 210 && p.code != 220 && p.code != 230) continue;
        auto v = number(p);
        if (!v) return v.error();
        (p.code == 210 ? x : p.code == 220 ? y : z) = v.value();
    }
    const double len = std::sqrt(x * x + y * y + z * z);
    if (!(len > 0) || std::fabs(x) > 1e-9 * len || std::fabs(y) > 1e-9 * len) return Extrusion{false, false};
    return Extrusion{true, z < 0};
}

class Reader {
public:
    Reader(const DxfImportOptions& o, DxfImport& r) : opt_(o), res_(r) {}

    core::Result<void> run(std::string_view text) {
        Lexer lx(text);
        bool sawEntities = false;
        while (true) {
            auto p = lx.next();
            if (!p) return p.error();
            if (!p.value()) break;
            const Pair& pr = *p.value();
            if (pr.code != 0) continue; // stray pairs outside sections (e.g. 999 comments)
            if (pr.value == "EOF") return finish(sawEntities, lx.line());
            if (pr.value != "SECTION") continue;
            auto name = lx.next();
            if (!name) return name.error();
            if (!name.value() || name.value()->code != 2)
                return parseError(pr.line, "SECTION is not followed by its name (group code 2).");
            const std::string_view sec = name.value()->value;
            core::Result<void> r;
            if (sec == "HEADER") r = header(lx, pr.line);
            else if (sec == "ENTITIES") {
                sawEntities = true;
                r = entities(lx, pr.line);
            } else r = skipSection(lx, pr.line, sec);
            if (!r) return r;
        }
        return parseError(lx.line(), "the file ends without the EOF marker (the file is truncated).");
    }

private:
    core::Result<void> skipSection(Lexer& lx, std::size_t start, std::string_view name) {
        while (true) {
            auto p = lx.next();
            if (!p) return p.error();
            if (!p.value())
                return parseError(lx.line(), "the file ends inside the " + std::string(name) + " section started at line " +
                                                 std::to_string(start) + " (the file is truncated).");
            if (p.value()->code == 0 && p.value()->value == "ENDSEC") return {};
        }
    }

    core::Result<void> header(Lexer& lx, std::size_t start) {
        bool inInsUnits = false;
        while (true) {
            auto p = lx.next();
            if (!p) return p.error();
            if (!p.value())
                return parseError(lx.line(), "the file ends inside the HEADER section started at line " +
                                                 std::to_string(start) + " (the file is truncated).");
            const Pair& pr = *p.value();
            if (pr.code == 0 && pr.value == "ENDSEC") return {};
            if (pr.code == 9) inInsUnits = pr.value == "$INSUNITS";
            else if (inInsUnits && pr.code == 70) {
                auto v = integer(pr);
                if (!v) return v.error();
                insUnits_ = v.value();
                insUnitsLine_ = pr.line;
            }
        }
    }

    core::Result<void> entities(Lexer& lx, std::size_t start) {
        std::optional<Entity> cur;
        while (true) {
            auto p = lx.next();
            if (!p) return p.error();
            if (!p.value())
                return parseError(lx.line(), "the file ends inside the ENTITIES section started at line " +
                                                 std::to_string(start) + " (the file is truncated).");
            const Pair& pr = *p.value();
            if (pr.code != 0) {
                if (!cur) return parseError(pr.line, "group code " + std::to_string(pr.code) + " before the first entity.");
                cur->pairs.push_back(pr);
                continue;
            }
            if (cur) {
                if (auto r = entity(*cur); !r) return r;
                cur.reset();
            }
            if (pr.value == "ENDSEC") return closePolyline(pr.line);
            if (++res_.entityCount > opt_.maxEntities)
                return core::makeError(core::ErrorCode::Unsupported,
                                       "DXF line " + std::to_string(pr.line) + ": more than " +
                                           std::to_string(opt_.maxEntities) + " entities; the file is refused.");
            cur = Entity{std::string(pr.value), pr.line, {}};
        }
    }

    bool onLayer(const Entity& e) {
        if (opt_.layer.empty() || equalsIgnoreCase(e.layer(), opt_.layer)) return true;
        ++otherLayer_;
        return false;
    }

    void unsupported(const std::string& what, std::size_t line) {
        auto& u = unsupported_[what];
        if (u.first++ == 0) u.second = line;
    }

    core::Result<void> entity(const Entity& e) {
        if (e.type == "VERTEX") return vertex(e);
        if (e.type == "SEQEND") return closePolyline(e.line);
        if (auto r = closePolyline(e.line); !r) return r;
        if (e.type == "INSERT") {
            if (!onLayer(e)) return {};
            ++inserts_;
            if (firstInsertLine_ == 0) {
                firstInsertLine_ = e.line;
                for (const auto& p : e.pairs)
                    if (p.code == 2) firstInsertBlock_ = std::string(p.value);
            }
            return {};
        }
        const bool known = e.type == "LINE" || e.type == "ARC" || e.type == "CIRCLE" || e.type == "LWPOLYLINE" ||
                           e.type == "POLYLINE";
        if (!known) {
            if (onLayer(e)) unsupported(e.type, e.line);
            return {};
        }
        if (!onLayer(e)) {
            if (e.type == "POLYLINE") skippingPolyline_ = true;
            return {};
        }
        auto ex = extrusionOf(e);
        if (!ex) return ex.error();
        if (!ex.value().supported && e.type != "LINE") {
            unsupported(e.type + " with an extrusion direction other than (0,0,1) or (0,0,-1)", e.line);
            if (e.type == "POLYLINE") skippingPolyline_ = true;
            return {};
        }
        mirrored_ = ex.value().mirrored && e.type != "LINE"; // LINE points are world coordinates
        if (e.type == "LINE") return line(e);
        if (e.type == "ARC" || e.type == "CIRCLE") return arc(e);
        if (e.type == "LWPOLYLINE") return lwpolyline(e);
        return polylineStart(e);
    }

    core::Result<void> values(const Entity& e, std::map<int, double>& out) {
        for (const auto& p : e.pairs) {
            if (!out.count(p.code)) continue;
            auto v = number(p);
            if (!v) return v.error();
            out[p.code] = v.value();
        }
        return {};
    }

    core::Result<void> line(const Entity& e) {
        std::map<int, double> v{{10, 0}, {20, 0}, {11, 0}, {21, 0}};
        if (auto r = values(e, v); !r) return r;
        addChain({{{v[10], v[20]}, {v[11], v[21]}, 0.0}}, e, false);
        return {};
    }

    core::Result<void> arc(const Entity& e) {
        std::map<int, double> v{{10, 0}, {20, 0}, {40, 0}, {50, 0}, {51, 360}};
        if (auto r = values(e, v); !r) return r;
        if (!(v[40] > 0)) {
            unsupported(e.type + " with radius <= 0", e.line);
            return {};
        }
        double sweep = 2 * kPi;
        double a0 = 0;
        if (e.type == "ARC") {
            a0 = v[50] * kPi / 180;
            double deg = std::fmod(v[51] - v[50], 360.0);
            if (deg <= 1e-9) deg += 360.0;
            sweep = deg * kPi / 180;
        }
        std::vector<Piece> pieces;
        appendArc(pieces, {v[10], v[20]}, v[40], a0, sweep);
        addChain(std::move(pieces), e, e.type == "CIRCLE" || sweep >= 2 * kPi - 1e-12);
        return {};
    }

    core::Result<void> lwpolyline(const Entity& e) {
        std::vector<std::pair<Vec2, double>> pts;
        long long flags = 0;
        for (const auto& p : e.pairs) {
            if (p.code == 70) {
                auto f = integer(p);
                if (!f) return f.error();
                flags = f.value();
            } else if (p.code == 10 || p.code == 20 || p.code == 42) {
                auto v = number(p);
                if (!v) return v.error();
                if (p.code == 10) pts.push_back({{v.value(), 0}, 0.0});
                else if (pts.empty()) return parseError(p.line, "LWPOLYLINE value before its first vertex (code 10).");
                else if (p.code == 20) pts.back().first.y = v.value();
                else pts.back().second = v.value();
            }
        }
        return polyline(std::move(pts), (flags & 1) != 0, e);
    }

    core::Result<void> polylineStart(const Entity& e) {
        long long flags = 0;
        for (const auto& p : e.pairs)
            if (p.code == 70) {
                auto f = integer(p);
                if (!f) return f.error();
                flags = f.value();
            }
        if (flags & (8 | 16 | 64)) {
            unsupported("POLYLINE (3D polyline, mesh or polyface)", e.line);
            skippingPolyline_ = true;
            return {};
        }
        poly_ = Poly{e, (flags & 1) != 0, mirrored_, {}};
        return {};
    }

    core::Result<void> vertex(const Entity& e) {
        if (skippingPolyline_) return {};
        if (!poly_) {
            unsupported("VERTEX outside a POLYLINE", e.line);
            return {};
        }
        std::map<int, double> v{{10, 0}, {20, 0}, {42, 0}, {70, 0}};
        if (auto r = values(e, v); !r) return r;
        if (static_cast<long long>(v[70]) & 16) return {}; // spline frame control point: not on the curve
        if (poly_->points.size() >= opt_.maxLoopVertices * 4)
            return core::makeError(core::ErrorCode::Unsupported, "DXF line " + std::to_string(e.line) +
                                                                     ": POLYLINE has too many vertices; the file is refused.");
        poly_->points.push_back({{v[10], v[20]}, v[42]});
        return {};
    }

    core::Result<void> closePolyline(std::size_t) {
        skippingPolyline_ = false;
        if (!poly_) return {};
        Poly p = std::move(*poly_);
        poly_.reset();
        mirrored_ = p.mirrored;
        return polyline(std::move(p.points), p.closed, p.entity);
    }

    core::Result<void> polyline(std::vector<std::pair<Vec2, double>> pts, bool closed, const Entity& e) {
        if (pts.size() < 2) {
            unsupported(e.type + " with fewer than 2 vertices", e.line);
            return {};
        }
        std::vector<Piece> pieces;
        const std::size_t n = pts.size();
        for (std::size_t i = 0; i + 1 < n; ++i) appendBulge(pieces, pts[i].first, pts[i + 1].first, pts[i].second);
        if (closed) appendBulge(pieces, pts[n - 1].first, pts[0].first, pts[n - 1].second);
        addChain(std::move(pieces), e, closed);
        return {};
    }

    void addChain(std::vector<Piece> pieces, const Entity& e, bool closed) {
        Chain c{{}, e.line, e.layer()};
        for (Piece p : pieces) {
            if (mirrored_) {
                p.a.x = -p.a.x;
                p.b.x = -p.b.x;
                p.bulge = -p.bulge;
            }
            c.pieces.push_back(p);
        }
        if (c.pieces.empty()) return;
        (closed ? closed_ : open_).push_back(std::move(c));
    }

    core::Result<void> finish(bool sawEntities, std::size_t line) {
        if (poly_) {
            if (auto r = closePolyline(line); !r) return r;
        }
        if (!sawEntities) return parseError(line, "the file has no ENTITIES section.");
        double scale = 1.0;
        if (opt_.units) {
            res_.unit = *opt_.units;
        } else if (!insUnits_ || *insUnits_ == 0) {
            res_.unit = DxfUnit::Millimetre;
            res_.diagnostics.push_back({core::Severity::Warning, "formats.dxf.units_assumed",
                                        std::string("The file does not state its drawing unit ($INSUNITS ") +
                                            (insUnits_ ? "is 0, unitless" : "is absent") +
                                            "); millimetres were assumed. Use the units option if this is wrong.",
                                        {}});
        } else {
            switch (*insUnits_) {
            case 1: res_.unit = DxfUnit::Inch; break;
            case 2: res_.unit = DxfUnit::Foot; break;
            case 4: res_.unit = DxfUnit::Millimetre; break;
            case 5: res_.unit = DxfUnit::Centimetre; break;
            case 6: res_.unit = DxfUnit::Metre; break;
            default:
                return core::makeError(core::ErrorCode::Unsupported,
                                       "DXF line " + std::to_string(insUnitsLine_) + ": $INSUNITS " +
                                           std::to_string(*insUnits_) +
                                           " is not supported (mm, cm, m, in and ft are); give the unit explicitly.");
            }
        }
        scale = millimetresPer(res_.unit);
        for (auto* list : {&closed_, &open_})
            for (auto& c : *list) {
                for (auto& p : c.pieces) {
                    p.a = p.a * scale;
                    p.b = p.b * scale;
                    for (double v : {p.a.x, p.a.y, p.b.x, p.b.y})
                        if (!(std::fabs(v) <= kMaxCoordinateMm))
                            return core::makeError(core::ErrorCode::Unsupported,
                                                   "DXF line " + std::to_string(c.line) + ": coordinate " + num(v) +
                                                       " mm is beyond the supported range of 1e8 mm.");
                }
                std::vector<Piece> kept;
                for (const auto& p : c.pieces)
                    if (geometry::length(p.b - p.a) > 1e-9) kept.push_back(p);
                    else ++degenerate_;
                c.pieces = std::move(kept);
            }
        std::vector<Chain> loops;
        for (auto& c : closed_) {
            if (c.pieces.empty()) continue;
            // Defensive: a closed entity always ends where it starts (closing piece added).
            if (geometry::length(c.pieces.back().b - c.pieces.front().a) > opt_.chainTolerance) open_.push_back(c);
            else loops.push_back(std::move(c));
        }
        std::vector<Chain> open;
        for (auto& c : open_)
            if (!c.pieces.empty()) open.push_back(std::move(c));
        std::stable_sort(open.begin(), open.end(), [](const Chain& a, const Chain& b) { return a.line < b.line; });
        dxf::chainPieces(std::move(open), opt_.chainTolerance, loops, res_.diagnostics);
        std::stable_sort(loops.begin(), loops.end(), [](const Chain& a, const Chain& b) { return a.line < b.line; });
        for (const auto& c : loops) {
            DxfLoop loop;
            if (dxf::makeLoop(c, opt_.maxLoopVertices, loop, res_.diagnostics)) res_.loops.push_back(std::move(loop));
        }
        report();
        return {};
    }

    void report() {
        auto& d = res_.diagnostics;
        if (inserts_ > 0)
            d.push_back({core::Severity::Warning, "formats.dxf.insert_unsupported",
                         std::to_string(inserts_) + " INSERT (block reference) entit" + (inserts_ == 1 ? "y" : "ies") +
                             " not imported, first at line " + std::to_string(firstInsertLine_) + " (block '" +
                             firstInsertBlock_ + "'): blocks are not supported; explode them in the CAD program.",
                         {}});
        for (const auto& [type, u] : unsupported_)
            d.push_back({core::Severity::Warning, "formats.dxf.entity_unsupported",
                         std::to_string(u.first) + " " + type + " entit" + (u.first == 1 ? "y" : "ies") +
                             " not imported (first at line " + std::to_string(u.second) +
                             "); supported are LINE, ARC, CIRCLE, LWPOLYLINE and 2D POLYLINE.",
                         {}});
        if (degenerate_ > 0)
            d.push_back({core::Severity::Info, "formats.dxf.zero_length",
                         std::to_string(degenerate_) + " zero-length segment(s) were dropped.", {}});
        if (otherLayer_ > 0)
            d.push_back({core::Severity::Info, "formats.dxf.layer_skipped",
                         std::to_string(otherLayer_) + " entit" + (otherLayer_ == 1 ? "y" : "ies") +
                             " on layers other than '" + opt_.layer + "' skipped.",
                         {}});
    }

    struct Poly {
        Entity entity;
        bool closed = false;
        bool mirrored = false;
        std::vector<std::pair<Vec2, double>> points;
    };

    const DxfImportOptions& opt_;
    DxfImport& res_;
    std::optional<long long> insUnits_;
    std::size_t insUnitsLine_ = 0;
    std::vector<Chain> closed_, open_;
    std::optional<Poly> poly_;
    bool skippingPolyline_ = false;
    bool mirrored_ = false;
    std::size_t inserts_ = 0, firstInsertLine_ = 0, otherLayer_ = 0, degenerate_ = 0;
    std::string firstInsertBlock_;
    std::map<std::string, std::pair<std::size_t, std::size_t>> unsupported_; ///< type -> (count, first line)
};

} // namespace

core::Result<DxfImport> importDxf(std::string_view text, const DxfImportOptions& options) {
    if (!(options.chainTolerance > 0 && options.chainTolerance <= 10))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "DXF chain tolerance " + num(options.chainTolerance) + " mm must be in (0, 10] mm.");
    if (text.size() > options.maxBytes)
        return core::makeError(core::ErrorCode::Unsupported, "The DXF text has " + std::to_string(text.size()) +
                                                                 " bytes, more than the limit of " +
                                                                 std::to_string(options.maxBytes) + ".");
    if (text.substr(0, 18) == "AutoCAD Binary DXF")
        return core::makeError(core::ErrorCode::Unsupported, "Binary DXF is not supported; save the drawing as ASCII DXF.");
    DxfImport result;
    Reader reader(options, result);
    if (auto r = reader.run(text); !r) return r.error();
    return result;
}

core::Result<DxfImport> importDxfFile(const std::filesystem::path& path, const DxfImportOptions& options) {
    auto text = readFile(path, ReadLimits{options.maxBytes});
    if (!text) return text.error();
    return importDxf(text.value(), options);
}

} // namespace fos::formats
