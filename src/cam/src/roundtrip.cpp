// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/cam/roundtrip.hpp"

#include "fos/core/units.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <map>
#include <optional>

namespace fos::cam {

using core::ErrorCode;
using core::makeError;

namespace {

core::Error lineError(std::size_t line, const std::string& msg) {
    return makeError(ErrorCode::ParseError, "Line " + std::to_string(line) + ": " + msg);
}

bool isInt(double v) { return std::fabs(v - std::round(v)) < 1e-9; }

} // namespace

core::Result<std::vector<Motion>> parseGenericIso(std::string_view text) {
    std::vector<Motion> out;
    bool g21 = false, g90 = false, ended = false;
    int modal = -1; // 0 or 1
    std::optional<double> x, y, z;
    std::size_t lineNo = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        ++lineNo;
        // Strip comments.
        std::string code;
        bool inComment = false;
        for (char c : line) {
            if (inComment) { if (c == ')') inComment = false; continue; }
            if (c == '(') { inComment = true; continue; }
            if (c == ';') break;
            code += c;
        }
        if (inComment) return lineError(lineNo, "unterminated comment.");
        // Tokenize words.
        std::vector<std::pair<char, double>> words;
        std::size_t i = 0;
        while (i < code.size()) {
            if (std::isspace(static_cast<unsigned char>(code[i]))) { ++i; continue; }
            const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(code[i])));
            if (!std::isalpha(static_cast<unsigned char>(letter)))
                return lineError(lineNo, std::string("unexpected character '") + code[i] + "'.");
            ++i;
            std::size_t j = i;
            while (j < code.size() && (std::isdigit(static_cast<unsigned char>(code[j])) || code[j] == '.' ||
                                       code[j] == '-' || code[j] == '+'))
                ++j;
            double v = 0;
            const char* b = code.data() + i;
            if (j < code.size() + 1 && b != code.data() + j && *b == '+') ++b;
            auto res = std::from_chars(b, code.data() + j, v);
            if (res.ec != std::errc() || res.ptr != code.data() + j)
                return lineError(lineNo, std::string("word '") + letter + "' has no valid number.");
            words.emplace_back(letter, v);
            i = j;
        }
        if (words.empty()) continue;
        if (ended) return lineError(lineNo, "code after M30.");
        std::optional<double> wx, wy, wz, wf, wp, ws, wt;
        std::optional<int> motionG;
        bool m3 = false, m5 = false, m6 = false, g4 = false;
        for (auto [l, v] : words) {
            const std::string w = std::string(1, l) + core::formatLength(v, 3);
            switch (l) {
            case 'G':
                if (!isInt(v)) return lineError(lineNo, "'" + w + "' is outside the supported subset.");
                switch (static_cast<int>(std::lround(v))) {
                case 0: motionG = 0; break;
                case 1: motionG = 1; break;
                case 4: g4 = true; break;
                case 17: case 94: break;
                case 21: g21 = true; break;
                case 90: g90 = true; break;
                default: return lineError(lineNo, "'" + w + "' is outside the supported subset (G0 G1 G4 G17 G21 G90 G94).");
                }
                break;
            case 'M':
                if (!isInt(v)) return lineError(lineNo, "'" + w + "' is outside the supported subset.");
                switch (static_cast<int>(std::lround(v))) {
                case 3: m3 = true; break;
                case 5: m5 = true; break;
                case 6: m6 = true; break;
                case 30: ended = true; break;
                default: return lineError(lineNo, "'" + w + "' is outside the supported subset (M3 M5 M6 M30).");
                }
                break;
            case 'X': wx = v; break;
            case 'Y': wy = v; break;
            case 'Z': wz = v; break;
            case 'F': wf = v; break;
            case 'P': wp = v; break;
            case 'S': ws = v; break;
            case 'T': wt = v; break;
            default: return lineError(lineNo, std::string("word '") + l + "' is outside the supported subset.");
            }
        }
        const bool hasAxes = wx || wy || wz;
        if ((hasAxes || g4 || m3 || m6) && !(g21 && g90))
            return lineError(lineNo, "motion before the G21 G90 preamble.");
        if (m6) {
            if (!wt || !isInt(*wt)) return lineError(lineNo, "M6 without a tool number T.");
            out.push_back(Motion::toolChange(static_cast<int>(std::lround(*wt))));
        } else if (wt) {
            return lineError(lineNo, "T word without M6.");
        }
        if (m3) {
            if (!ws) return lineError(lineNo, "M3 without a spindle speed S.");
            out.push_back(Motion::spindleOn(*ws));
        } else if (ws) {
            return lineError(lineNo, "S word without M3.");
        }
        if (m5) out.push_back(Motion::spindleOff());
        if (g4) {
            if (!wp) return lineError(lineNo, "G4 without P.");
            out.push_back(Motion::dwell(*wp));
        } else if (wp) {
            return lineError(lineNo, "P word without G4.");
        }
        if (motionG) modal = *motionG;
        if (hasAxes) {
            if (modal < 0) return lineError(lineNo, "coordinates without G0 or G1.");
            if (modal == 0 && wz && !wx && !wy) {
                Motion mo = Motion::rapidZ(*wz);
                if (x && y) mo.target = {*x, *y, *wz};
                out.push_back(mo);
                z = wz;
            } else {
                if (wx) x = wx;
                if (wy) y = wy;
                if (wz) z = wz;
                if (!x || !y || !z) return lineError(lineNo, "move with an unknown axis position.");
                if (modal == 0) {
                    out.push_back(Motion::rapid({*x, *y, *z}));
                } else {
                    if (!wf) return lineError(lineNo, "G1 without an explicit F word.");
                    out.push_back(Motion::linear({*x, *y, *z}, *wf));
                }
            }
        } else if (wf) {
            return lineError(lineNo, "F word without a move.");
        }
    }
    if (!ended) return makeError(ErrorCode::ParseError, "The program does not end with M30.");
    return out;
}

RoundTripReport compareMotions(const std::vector<Motion>& expected, const std::vector<Motion>& parsed,
                               double tolerance) {
    RoundTripReport r;
    r.expectedMotions = expected.size();
    r.parsedMotions = parsed.size();
    auto f = [](double v) { return core::formatLength(v, 4); };
    if (expected.size() != parsed.size())
        r.differences.push_back("motion count differs: IR has " + std::to_string(expected.size()) + ", text has " +
                                std::to_string(parsed.size()) + ".");
    const std::size_t n = std::min(expected.size(), parsed.size());
    for (std::size_t i = 0; i < n; ++i) {
        const Motion& e = expected[i];
        const Motion& p = parsed[i];
        const std::string at = "motion #" + std::to_string(i) + ": ";
        if (e.kind != p.kind || e.zOnly != p.zOnly) {
            r.differences.push_back(at + "IR '" + describe(e) + "' but text '" + describe(p) + "'.");
            continue;
        }
        auto near = [&](double a, double b, double tol, const char* what) {
            if (std::fabs(a - b) > tol)
                r.differences.push_back(at + what + " " + f(a) + " in the IR but " + f(b) + " in the text.");
        };
        switch (e.kind) {
        case MotionKind::Linear: near(e.feed, p.feed, 0.05, "feed"); [[fallthrough]];
        case MotionKind::Rapid:
            if (!e.zOnly) {
                near(e.target.x, p.target.x, tolerance, "X");
                near(e.target.y, p.target.y, tolerance, "Y");
            }
            near(e.target.z, p.target.z, tolerance, "Z");
            break;
        case MotionKind::ToolChange:
            if (e.toolNumber != p.toolNumber) r.differences.push_back(at + "tool number differs.");
            break;
        case MotionKind::SpindleOn: near(e.rpm, p.rpm, 0.5, "rpm"); break;
        case MotionKind::SpindleOff: break;
        case MotionKind::Dwell: near(e.seconds, p.seconds, tolerance, "dwell"); break;
        }
    }
    r.matches = r.differences.empty();
    return r;
}

core::Result<RoundTripReport> checkRoundTrip(const Program& program, std::string_view text) {
    auto parsed = parseGenericIso(text);
    if (!parsed) return parsed.error();
    return compareMotions(program.motions, parsed.value());
}

} // namespace fos::cam
