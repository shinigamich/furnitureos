// SPDX-License-Identifier: AGPL-3.0-only
// fos linear, fos edgeband-rolls
#include "common.hpp"

#include "fos/core/units.hpp"
#include "fos/linear/linear.hpp"
#include "fos/linear/svg.hpp"
#include "fos/production/edge_band_rolls.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace fos::cli::detail {
namespace {

const char* kLinearHelp =
    "Usage: fos linear <pieces.csv> --stock <length>[,<length>...] [--kerf K] [--trim T]\n"
    "                  [--min-offcut M] [--svg-dir DIR]\n\n"
    "1D cutting-stock optimisation (bars, profiles, rails). The CSV has a header row with the\n"
    "columns label;length;quantity (any order, case-insensitive; ',' or ';' separated; with ';'\n"
    "a decimal comma is accepted). --stock lists bar lengths in mm, each available in any\n"
    "number. --kerf is the saw width, --trim is removed at EACH bar end, offcuts of at least\n"
    "--min-offcut mm are reported as reusable (all mm, default 0). The optimiser is a HEURISTIC\n"
    "(first-/best-fit decreasing + improvement): the bar count is optimal only when it equals the\n"
    "length-based lower bound (\"proven minimal\"). Every plan is checked independently\n"
    "(linear::validateLinearPlan). --svg-dir writes DIR/linear-plan.svg (1 unit = 1 mm).\n"
    "Exit code 1 when pieces could not be placed or the validator reports an error.\n";

const char* kRollsHelp =
    "Usage: fos edgeband-rolls <file.fosp> [--roll-length L]\n\n"
    "Plans edge-band rolls: per edge band, every banded edge (or contour strip) is a piece of\n"
    "applied length + 2 x the project's overhang per end, cut from rolls of L mm (default\n"
    "100000 = 100 m, any number). Prints strips, rolls needed and utilisation (HEURISTIC plan,\n"
    "1D optimiser) and the independent validator result. Exit code 1 when the validator reports\n"
    "an error or the project has model errors.\n";

std::string pct(double v) {
    return core::formatLength(100.0 * v, 2) + " %";
}

std::string trim(std::string s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    s = s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

std::vector<std::string> split(const std::string& line, char sep) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char c : line) {
        if (c == '"') quoted = !quoted;
        if (c == sep && !quoted) {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(trim(cur));
    return out;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// Pieces from CSV text; InvalidArgument / ParseError naming the line on bad content.
core::Result<std::vector<linear::Piece>> parsePiecesCsv(std::string text) {
    if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    char sep = ',';
    int cl = -1, cn = -1, cq = -1;
    std::vector<linear::Piece> pieces;
    std::vector<std::pair<std::string, int>> labels;
    while (std::getline(in, line)) {
        ++lineNo;
        if (trim(line).empty()) continue;
        if (cn < 0) {
            sep = line.find(';') != std::string::npos ? ';' : ',';
            const auto h = split(line, sep);
            for (std::size_t i = 0; i < h.size(); ++i) {
                const auto k = lower(h[i]);
                if (k == "label" || k == "name") cl = static_cast<int>(i);
                if (k == "length") cn = static_cast<int>(i);
                if (k == "quantity" || k == "qty" || k == "count") cq = static_cast<int>(i);
            }
            if (cn < 0 || cq < 0)
                return core::makeError(core::ErrorCode::ParseError,
                                       "line " + std::to_string(lineNo) +
                                           ": the header must name the columns 'length' and 'quantity' (and "
                                           "optionally 'label'), found '" + trim(line) + "'.");
            continue;
        }
        const auto f = split(line, sep);
        auto field = [&](int c) { return c >= 0 && c < static_cast<int>(f.size()) ? f[static_cast<std::size_t>(c)] : ""; };
        std::string len = field(cn);
        if (sep == ';') std::replace(len.begin(), len.end(), ',', '.');
        const auto length = parseNumber(len);
        const auto qty = parseNumber(field(cq));
        if (!length || *length <= 0)
            return core::makeError(core::ErrorCode::ParseError, "line " + std::to_string(lineNo) + ": length '" +
                                                                    field(cn) + "' is not a positive number.");
        if (!qty || *qty < 1 || *qty != static_cast<double>(static_cast<long>(*qty)) || *qty > 100000)
            return core::makeError(core::ErrorCode::ParseError, "line " + std::to_string(lineNo) + ": quantity '" +
                                                                    field(cq) + "' is not a whole number >= 1.");
        std::string label = field(cl);
        if (label.empty()) label = "row " + std::to_string(lineNo);
        pieces.push_back(linear::Piece{label, label, *length, static_cast<int>(*qty)});
        labels.emplace_back(label, lineNo);
    }
    if (pieces.empty()) return core::makeError(core::ErrorCode::ParseError, "the CSV contains no pieces.");
    // Ids must be unique: repeated labels get " (line N)".
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const auto n = std::count_if(labels.begin(), labels.end(),
                                     [&](const auto& l) { return l.first == labels[i].first; });
        if (n > 1) pieces[i].id = labels[i].first + " (line " + std::to_string(labels[i].second) + ")";
    }
    return pieces;
}

std::optional<double> nonNegative(const ParsedArgs& a, const char* name, Io& io, bool& bad) {
    const auto v = a.value(name);
    if (!v) return 0.0;
    const auto d = parseNumber(*v);
    if (!d || *d < 0) {
        usageError(io, "linear", std::string("--") + name + " '" + *v + "' is not a number >= 0.");
        bad = true;
        return std::nullopt;
    }
    return d;
}

void printPlan(std::ostream& s, const linear::LinearResult& r, bool perCut) {
    s << "  pieces: " << r.requiredCount << " required, " << r.placedCount << " placed, " << r.unplaced.size()
      << " unplaced\n";
    for (std::size_t i = 0; i < r.bars.size(); ++i) {
        const auto& b = r.bars[i];
        s << "  bar " << i + 1 << ": " << b.stockId << " " << core::formatLength(b.stockLength) << " mm, "
          << b.cuts.size() << " cut(s)";
        if (perCut) {
            s << ":";
            for (const auto& c : b.cuts)
                s << " " << c.pieceId << " " << core::formatLength(c.length) << " @" << core::formatLength(c.start);
        }
        s << "; offcut " << core::formatLength(b.offcutLength) << " mm (" << (b.offcutReusable ? "reusable" : "waste")
          << "), utilisation " << pct(b.utilisation) << "\n";
    }
    s << "  total: " << r.bars.size() << " bar(s), lower bound " << r.barCountLowerBound << " ("
      << (r.isBarCountProvenMinimal ? "proven minimal" : "not proven minimal") << "), utilisation "
      << pct(r.utilisation) << " (heuristic result, " << r.ordersTried << " orders tried), kerf "
      << core::formatLength(r.totalKerfLoss) << " mm, waste " << core::formatLength(r.totalWaste) << " mm\n";
    for (const auto& u : r.unplaced) s << "  UNPLACED: " << u.message << "\n";
}

} // namespace

int cmdLinear(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"stock"}, {"kerf"}, {"trim"}, {"min-offcut"}, {"svg-dir"}});
    if (!parsed) return usageError(io, "linear", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kLinearHelp;
        return kExitOk;
    }
    if (a.positional.size() != 1) return usageError(io, "linear", "expected exactly one pieces CSV file.");
    if (!a.value("stock")) return usageError(io, "linear", "--stock <length>[,<length>...] is required.");
    linear::LinearJob job;
    for (const auto& t : split(*a.value("stock"), ',')) {
        const auto len = parseNumber(t);
        if (!len || *len <= 0) return usageError(io, "linear", "stock length '" + t + "' is not a positive number.");
        const std::string id = "stock" + core::formatLength(*len);
        if (std::none_of(job.stock.begin(), job.stock.end(), [&](const auto& s) { return s.id == id; }))
            job.stock.push_back(linear::Stock{id, *len, 0, true, 0, false, 0});
    }
    bool bad = false;
    const auto kerf = nonNegative(a, "kerf", io, bad);
    const auto trimLen = nonNegative(a, "trim", io, bad);
    const auto minOff = nonNegative(a, "min-offcut", io, bad);
    if (bad) return kExitUsageOrIo;
    job.kerf = *kerf;
    job.trimStart = job.trimEnd = *trimLen;
    job.minReusableOffcut = *minOff;

    const auto path = pathFromUtf8(a.positional[0]);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        io.err << "fos linear: cannot read '" << pathToUtf8(path) << "'.\n";
        return kExitUsageOrIo;
    }
    std::ostringstream text;
    text << in.rdbuf();
    auto pieces = parsePiecesCsv(text.str());
    if (!pieces) {
        io.err << "fos linear: " << pathToUtf8(path) << ": " << pieces.error().message << "\n";
        return kExitUsageOrIo;
    }
    job.pieces = std::move(pieces.value());
    auto result = linear::optimizeLinear(job);
    if (!result) {
        io.err << "fos linear: " << result.error().message << "\n";
        return kExitUsageOrIo;
    }
    const auto& r = result.value();
    const auto findings = linear::validateLinearPlan(job, r);
    io.out << "Linear cutting plan (HEURISTIC: first-/best-fit decreasing + improvement)\n"
           << "  kerf " << core::formatLength(job.kerf) << " mm, trim " << core::formatLength(job.trimStart)
           << " mm per bar end, minimum reusable offcut " << core::formatLength(job.minReusableOffcut) << " mm\n";
    printPlan(io.out, r, true);
    io.out << "  validator (linear::validateLinearPlan): "
           << (findings.empty() ? std::string("no findings") : std::to_string(findings.size()) + " errors") << "\n";
    printDiagnostics(io.out, findings, "    ");
    if (const auto dir = a.value("svg-dir")) {
        const auto d = pathFromUtf8(*dir);
        if (!ensureOutputDirectory(io, "linear", d)) return kExitUsageOrIo;
        if (!writeOutputFile(io, "linear", d / "linear-plan.svg", linear::renderLinearSvg(job, r)))
            return kExitUsageOrIo;
        io.out << "  wrote " << pathToUtf8(d / "linear-plan.svg") << "\n";
    }
    return findings.empty() && r.unplaced.empty() ? kExitOk : kExitValidationErrors;
}

int cmdEdgebandRolls(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"roll-length"}});
    if (!parsed) return usageError(io, "edgeband-rolls", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kRollsHelp;
        return kExitOk;
    }
    if (a.positional.size() != 1) return usageError(io, "edgeband-rolls", "expected exactly one project file.");
    production::EdgeBandRollOptions options;
    if (const auto v = a.value("roll-length")) {
        const auto d = parseNumber(*v);
        if (!d || *d <= 0)
            return usageError(io, "edgeband-rolls", "--roll-length '" + *v + "' is not a positive number.");
        options.rollLength = *d;
    }
    auto loaded = loadProjectOrReport(io, "edgeband-rolls", a.positional[0]);
    if (!loaded) return kExitUsageOrIo;
    const std::string incomplete = resolveErrorNotice("edgeband-rolls", *loaded);
    options.edgeBanding = loaded->project.settings.edgeBanding;
    const auto jobs = production::edgeBandStripJobs(loaded->resolved.model, options);
    io.out << "Edge-band rolls (roll " << core::formatLength(options.rollLength) << " mm, overhang "
           << core::formatLength(options.edgeBanding.overhangPerEnd) << " mm per strip end; HEURISTIC 1D plan)\n";
    if (jobs.empty()) io.out << "  no banded edges\n";
    bool errors = false;
    for (const auto& j : jobs) {
        io.out << j.edgeBandId.str() << (j.missing ? " (MISSING from the model)" : " '" + j.name + "'") << ": "
               << j.stripCount << " strip(s), applied " << core::formatLength(j.appliedLength / 1000.0, 3)
               << " m\n";
        auto r = linear::optimizeLinear(j.job);
        if (!r) {
            io.out << "  optimisation refused: " << r.error().message << "\n";
            errors = true;
            continue;
        }
        const auto findings = linear::validateLinearPlan(j.job, r.value());
        io.out << "  rolls needed: " << r.value().bars.size() << "\n";
        printPlan(io.out, r.value(), false);
        io.out << "  validator (linear::validateLinearPlan): "
               << (findings.empty() ? std::string("no findings") : std::to_string(findings.size()) + " errors")
               << "\n";
        printDiagnostics(io.out, findings, "    ");
        errors = errors || !findings.empty() || !r.value().unplaced.empty();
    }
    if (!incomplete.empty()) {
        io.err << incomplete;
        errors = true;
    }
    return errors ? kExitValidationErrors : kExitOk;
}

} // namespace fos::cli::detail
