// SPDX-License-Identifier: AGPL-3.0-only
// `fos worktop`: a kitchen worktop over a straight run of cabinets (project/worktop.hpp).
#include "common.hpp"

#include "fos/project/document.hpp"
#include "fos/project/worktop.hpp"

#include <charconv>
#include <cmath>

namespace fos::cli::detail {
namespace {

const char* kWorktopHelp =
    "Usage: fos worktop <file.fosp> --items ID[,ID...] [--overhang MM] [--left-overhang MM]\n"
    "                   [--right-overhang MM] [--back-gap MM] [--material ID] [--band ID | --no-band]\n"
    "                   [--band-ends] [--band-back] [--max-length MM] [--sink X:W:D[:R[:S]]]\n"
    "                   [--hob X:W:D[:R[:S]]] [--id ITEM-ID] [--name NAME] [-o|--out OUT.fosp]\n\n"
    "Adds a worktop over a straight run of cabinet items and saves the project (to --out, or in\n"
    "place). The run is measured from the generated cabinets (fronts included) in the frame of the\n"
    "first listed cabinet; all cabinets must share its rotation and their tops must be level.\n"
    "--overhang: front overhang beyond the foremost front (default 20); side overhangs default 0;\n"
    "--back-gap: gap to the wall behind the cabinets (default 0).\n"
    "--material: worktop material id (thickness from the material); default: a generic 38 mm\n"
    "placeholder (stock 4100 x 650) added to the library. Longer than the usable stock length\n"
    "(or --max-length), the top is split into equal pieces joined by BUTT JOINTS (moved away from\n"
    "cut-outs); connector bolts are not modelled.\n"
    "Edge banding: only the front edge by default (--band, else the first library band wide\n"
    "enough, else a generic ABS 2 x 45 placeholder); --band-ends / --band-back add the free ends /\n"
    "the back edge; --no-band leaves every edge raw.\n"
    "--sink / --hob X:W:D[:R[:S]]: centre X along the run from the left face of the leftmost\n"
    "cabinet, size W (along the run) x D (depth), corner radius R (default 0), front setback S\n"
    "(default: centred in depth). Minimum distances are RULES OF THUMB, not a standard: 50 mm to\n"
    "the front/back edge, 100 mm to a free end, 150 mm to a joint, 100 mm between cut-outs; check\n"
    "the appliance installation sheet.\n"
    "Exit codes: 0 added, 1 refused (rule violated, run not straight, ...), 2 usage or I/O error.\n\n"
    "Example:\n"
    "  fos worktop kitchen.fosp --items cab1,cab2,cab3 --overhang 25 --sink 900:780:480:10 -o out.fosp\n";

std::vector<std::string> splitText(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t p = s.find(sep, start);
        out.push_back(s.substr(start, p == std::string::npos ? std::string::npos : p - start));
        if (p == std::string::npos) break;
        start = p + 1;
    }
    return out;
}

std::string mm(double v) {
    v = std::round(v * 10.0) / 10.0;
    if (std::fabs(v) < 1e-9) v = 0;
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed);
    return r.ec == std::errc() ? std::string(buf, r.ptr) : std::to_string(v);
}

/// "X:W:D[:R[:S]]" -> cut-out; error text on failure.
std::optional<std::string> parseCutout(const std::string& option, const std::string& text,
                                       project::WorktopCutoutKind kind, project::WorktopCutout& out) {
    const auto parts = splitText(text, ':');
    if (parts.size() < 3 || parts.size() > 5)
        return "--" + option + " expects X:W:D[:R[:S]] (mm), got '" + text + "'.";
    double v[5] = {0, 0, 0, 0, 0};
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto n = parseNumber(parts[i]);
        if (!n) return "--" + option + ": '" + parts[i] + "' in '" + text + "' is not a number.";
        v[i] = *n;
    }
    out.kind = kind;
    out.centerX = v[0];
    out.length = v[1];
    out.depth = v[2];
    out.cornerRadius = v[3];
    if (parts.size() == 5) out.frontSetback = v[4];
    return std::nullopt;
}

} // namespace

int cmdWorktop(const std::vector<std::string>& rawArgs, Io& io) {
    const char* cmd = "worktop";
    std::vector<std::string> args = rawArgs;
    for (auto& a : args)
        if (a == "-o") a = "--out";
    auto parsed = parseArgs(args, {{"items"},
                                   {"overhang"},
                                   {"left-overhang"},
                                   {"right-overhang"},
                                   {"back-gap"},
                                   {"material"},
                                   {"band"},
                                   {"no-band", false},
                                   {"band-ends", false},
                                   {"band-back", false},
                                   {"max-length"},
                                   {"sink"},
                                   {"hob"},
                                   {"id"},
                                   {"name"},
                                   {"out"}});
    if (!parsed) return usageError(io, cmd, parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kWorktopHelp;
        return kExitOk;
    }
    if (a.positional.size() != 1) return usageError(io, cmd, "expected exactly one <file.fosp>.");
    const auto items = a.value("items");
    if (!items || items->empty()) return usageError(io, cmd, "--items ID[,ID...] is required.");

    project::WorktopRequest req;
    for (const auto& id : splitText(*items, ','))
        if (!id.empty()) req.itemIds.push_back(id);
    const auto number = [&](const char* name, double& target) -> bool {
        if (auto t = a.value(name)) {
            const auto n = parseNumber(*t);
            if (!n) return false;
            target = *n;
        }
        return true;
    };
    for (auto [name, target] : {std::pair<const char*, double*>{"overhang", &req.frontOverhang},
                                {"left-overhang", &req.leftOverhang},
                                {"right-overhang", &req.rightOverhang},
                                {"back-gap", &req.backGap}})
        if (!number(name, *target))
            return usageError(io, cmd, std::string("--") + name + " must be a number (mm), got '" + *a.value(name) + "'.");
    if (auto t = a.value("max-length")) {
        const auto n = parseNumber(*t);
        if (!n) return usageError(io, cmd, "--max-length must be a number (mm), got '" + *t + "'.");
        req.maxPieceLength = *n;
    }
    if (a.has("no-band") && a.value("band")) return usageError(io, cmd, "--band and --no-band exclude each other.");
    req.materialId = a.value("material").value_or("");
    req.frontBandId = a.value("band").value_or("");
    req.bandFront = !a.has("no-band");
    req.bandLeftEnd = req.bandRightEnd = a.has("band-ends") && req.bandFront;
    req.bandBack = a.has("band-back") && req.bandFront;
    if (auto id = a.value("id")) req.id = *id;
    if (auto name = a.value("name")) req.name = *name;
    for (auto [option, kind] : {std::pair<const char*, project::WorktopCutoutKind>{"sink", project::WorktopCutoutKind::Sink},
                                {"hob", project::WorktopCutoutKind::Hob}}) {
        if (auto t = a.value(option)) {
            project::WorktopCutout c;
            if (auto e = parseCutout(option, *t, kind, c)) return usageError(io, cmd, *e);
            req.cutouts.push_back(c);
        }
    }

    const std::string& file = a.positional[0];
    const std::string out = a.value("out").value_or(file);
    auto loaded = loadProjectOrReport(io, cmd, file);
    if (!loaded) return kExitUsageOrIo;

    auto plan = project::planWorktop(loaded->project, req);
    if (!plan) {
        io.err << "fos worktop: refused: " << plan.error().message << "\nNothing was saved.\n";
        return kExitValidationErrors;
    }
    project::Document doc(loaded->project);
    if (auto r = doc.execute(project::commands::addWorktop(req)); !r) {
        io.err << "fos worktop: refused: " << r.error().message << "\nNothing was saved.\n";
        return kExitValidationErrors;
    }
    if (auto r = doc.save(pathFromUtf8(out)); !r) {
        io.err << "fos worktop: cannot save '" << out << "': " << r.error().message << "\n";
        return kExitUsageOrIo;
    }
    const project::WorktopPlan& w = plan.value();
    const std::string material = req.materialId.empty() ? project::kGenericWorktopMaterial : req.materialId;
    io.out << "Worktop over " << w.runOrder.size() << " cabinet(s) (";
    for (std::size_t i = 0; i < w.runOrder.size(); ++i) io.out << (i ? ", " : "") << w.runOrder[i];
    io.out << "): " << mm(w.length) << " x " << mm(w.depth) << " x " << mm(w.thickness) << " mm, material " << material
           << ", top at z = " << mm(w.top) << " mm.\n";
    if (w.items.size() == 1) {
        io.out << "One piece: item " << w.items.front().id() << ".\n";
    } else {
        io.out << w.items.size() << " pieces (longer than " << mm(w.usableStockLength)
               << " mm of usable stock), BUTT JOINTS at";
        for (double j : w.joints) io.out << " " << mm(j);
        io.out << " mm from the left end:";
        for (const auto& item : w.items) io.out << " " << item.id();
        io.out << ".\n";
    }
    for (std::size_t i = 0; i < req.cutouts.size(); ++i) {
        const auto& c = req.cutouts[i];
        io.out << "Cut-out " << project::toString(c.kind) << ": " << mm(c.length) << " x " << mm(c.depth)
               << " mm, centre " << mm(c.centerX) << " mm from the run start"
               << (c.cornerRadius > 0 ? ", R " + mm(c.cornerRadius) : std::string()) << ".\n";
    }
    if (!w.notes.empty()) {
        io.out << "Notes:\n";
        printDiagnostics(io.out, w.notes);
    }
    io.out << "Saved " << out << "\n";
    return kExitOk;
}

} // namespace fos::cli::detail
