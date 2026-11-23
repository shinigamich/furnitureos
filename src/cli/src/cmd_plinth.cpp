// SPDX-License-Identifier: AGPL-3.0-only
// `fos plinth`: a kitchen plinth (toe-kick) under a straight run of cabinets (project/plinth.hpp).
#include "common.hpp"

#include "fos/project/document.hpp"
#include "fos/project/plinth.hpp"

#include <charconv>
#include <cmath>

namespace fos::cli::detail {
namespace {

const char* kPlinthHelp =
    "Usage: fos plinth <file.fosp> --items ID[,ID...] [--setback MM] [--height MM]\n"
    "                  [--returns left,right] [--material ID] [--max-length MM] [--id ITEM-ID]\n"
    "                  [--name NAME] [-o|--out OUT.fosp]\n\n"
    "Adds a front plinth (toe-kick) board under a straight run of cabinet items and saves the\n"
    "project (to --out, or in place). Cabinets must stand on feet (plinth construction Feet);\n"
    "appliance gaps (dishwasher) are accepted: the plinth is interrupted at their end panels and\n"
    "the strip in front of the appliance becomes its own piece.\n"
    "--setback: plinth face behind the foremost carcass front (default 50, a RULE OF THUMB);\n"
    "--height: board height (default: the cabinets' plinth_height, i.e. the feet height);\n"
    "--returns left,right: end returns from the plinth back to the back of the end cabinet;\n"
    "--material: board material (default: the first cabinet's carcass material). Longer than the\n"
    "usable stock length (or --max-length), a segment is split into equal pieces with BUTT\n"
    "JOINTS; plinth clips and connectors are not modelled. Integrated dishwashers often need a\n"
    "lower or notched plinth for the door swing (rule of thumb; check the installation sheet).\n"
    "Exit codes: 0 added, 1 refused, 2 usage or I/O error.\n\n"
    "Example:\n"
    "  fos plinth kitchen.fosp --items cab1,dw,cab3 --setback 60 --returns right -o out.fosp\n";

std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t p = s.find(',', start);
        std::string part = s.substr(start, p == std::string::npos ? std::string::npos : p - start);
        if (!part.empty()) out.push_back(std::move(part));
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

} // namespace

int cmdPlinth(const std::vector<std::string>& rawArgs, Io& io) {
    const char* cmd = "plinth";
    std::vector<std::string> args = rawArgs;
    for (auto& a : args)
        if (a == "-o") a = "--out";
    auto parsed = parseArgs(args, {{"items"},
                                   {"setback"},
                                   {"height"},
                                   {"returns"},
                                   {"material"},
                                   {"max-length"},
                                   {"id"},
                                   {"name"},
                                   {"out"}});
    if (!parsed) return usageError(io, cmd, parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kPlinthHelp;
        return kExitOk;
    }
    if (a.positional.size() != 1) return usageError(io, cmd, "expected exactly one <file.fosp>.");
    const auto items = a.value("items");
    if (!items || items->empty()) return usageError(io, cmd, "--items ID[,ID...] is required.");

    project::PlinthRequest req;
    req.itemIds = splitList(*items);
    const auto number = [&](const char* name) -> std::optional<std::optional<double>> {
        const auto t = a.value(name);
        if (!t) return std::optional<double>{};
        const auto n = parseNumber(*t);
        if (!n) return std::nullopt;
        return std::optional<double>{*n};
    };
    for (const char* name : {"setback", "height", "max-length"}) {
        const auto v = number(name);
        if (!v) return usageError(io, cmd, std::string("--") + name + " must be a number (mm), got '" + *a.value(name) + "'.");
        if (!*v) continue;
        if (std::string_view(name) == "setback") req.setback = **v;
        else if (std::string_view(name) == "height") req.height = **v;
        else req.maxPieceLength = **v;
    }
    if (auto r = a.value("returns")) {
        for (const auto& side : splitList(*r)) {
            if (side == "left") req.returnLeft = true;
            else if (side == "right") req.returnRight = true;
            else return usageError(io, cmd, "--returns expects left, right or left,right, got '" + *r + "'.");
        }
    }
    req.materialId = a.value("material").value_or("");
    if (auto id = a.value("id")) req.id = *id;
    if (auto name = a.value("name")) req.name = *name;

    const std::string& file = a.positional[0];
    const std::string out = a.value("out").value_or(file);
    auto loaded = loadProjectOrReport(io, cmd, file);
    if (!loaded) return kExitUsageOrIo;

    auto plan = project::planPlinth(loaded->project, req);
    if (!plan) {
        io.err << "fos plinth: refused: " << plan.error().message << "\nNothing was saved.\n";
        return kExitValidationErrors;
    }
    project::Document doc(loaded->project);
    if (auto r = doc.execute(project::commands::addPlinth(req)); !r) {
        io.err << "fos plinth: refused: " << r.error().message << "\nNothing was saved.\n";
        return kExitValidationErrors;
    }
    if (auto r = doc.save(pathFromUtf8(out)); !r) {
        io.err << "fos plinth: cannot save '" << out << "': " << r.error().message << "\n";
        return kExitUsageOrIo;
    }
    const project::PlinthPlan& w = plan.value();
    io.out << "Plinth under " << w.runOrder.size() << " cabinet(s) (";
    for (std::size_t i = 0; i < w.runOrder.size(); ++i) io.out << (i ? ", " : "") << w.runOrder[i];
    io.out << "): run " << mm(w.length) << " mm, height " << mm(w.height) << " mm, " << mm(w.thickness)
           << " mm " << w.materialId << ", setback " << mm(req.setback) << " mm.\n";
    io.out << w.frontPieces << " front piece(s):";
    for (std::size_t k = 0; k < w.frontPieces; ++k)
        io.out << " " << w.items[k].id() << " (" << mm(w.pieces[k].first) << ".." << mm(w.pieces[k].second) << ")";
    io.out << ".\n";
    if (w.items.size() > w.frontPieces) {
        io.out << "End return(s):";
        for (std::size_t k = w.frontPieces; k < w.items.size(); ++k) io.out << " " << w.items[k].id();
        io.out << ".\n";
    }
    if (!w.notes.empty()) {
        io.out << "Notes:\n";
        printDiagnostics(io.out, w.notes);
    }
    io.out << "Saved " << out << "\n";
    return kExitOk;
}

} // namespace fos::cli::detail
