// SPDX-License-Identifier: AGPL-3.0-only
// `fos merge`: import furniture items (and the library entries they use) from another project.
#include "common.hpp"

#include "fos/cli/cli.hpp"
#include "fos/project/merge.hpp"

#include <sstream>

namespace fos::cli::detail {
namespace {

const char* kMergeHelp =
    "Usage: fos merge <target.fosp> <source.fosp> [--items a,b] [--policy keep|replace|rename]\n"
    "                 [--offset x,y,z] [-o out.fosp]\n\n"
    "Appends furniture items of the source project to the target project, with the materials,\n"
    "edge bands and hardware they reference, and saves the result atomically to -o\n"
    "(default: the target file). --items selects source item ids (default: all).\n"
    "An item id already used in the target is renamed: trailing digits are replaced by the first\n"
    "free number (cab1 -> cab3 when cab1 and cab2 exist). --offset (mm, default 0,0,0) moves\n"
    "every imported item. A library entry whose id exists in the target with different content:\n"
    "keep (default) uses the target's entry, replace overwrites it (target items using it change\n"
    "too), rename adds the source entry as 'merge.libN' and points the imported items at it.\n"
    "Identical entries are never duplicated. On any error nothing is saved.\n";

std::vector<std::string> splitComma(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream in(text);
    while (std::getline(in, cur, ','))
        if (!cur.empty()) out.push_back(cur);
    return out;
}

} // namespace

int cmdMerge(const std::vector<std::string>& rawArgs, Io& io) {
    // "-o out" is accepted as a short form of "--o out".
    std::vector<std::string> args;
    for (const auto& a : rawArgs) args.push_back(a == "-o" ? "--o" : a);
    auto parsed = parseArgs(args, {{"items"}, {"policy"}, {"offset"}, {"o"}});
    if (!parsed) return usageError(io, "merge", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kMergeHelp;
        return kExitOk;
    }
    if (a.positional.size() != 2) return usageError(io, "merge", "expected <target.fosp> <source.fosp>.");
    project::MergeRequest request;
    if (auto items = a.value("items")) {
        request.itemIds = splitComma(*items);
        if (request.itemIds.empty()) return usageError(io, "merge", "--items needs at least one item id.");
    }
    if (auto p = a.value("policy")) {
        if (*p == "keep") request.conflictPolicy = formats::ConflictPolicy::Skip;
        else if (*p == "replace") request.conflictPolicy = formats::ConflictPolicy::Replace;
        else if (*p == "rename") request.conflictPolicy = formats::ConflictPolicy::RenameWithNewId;
        else return usageError(io, "merge", "--policy must be keep, replace or rename, not '" + *p + "'.");
    }
    if (auto text = a.value("offset")) {
        const auto parts = splitComma(*text);
        std::optional<double> v[3];
        if (parts.size() == 3)
            for (std::size_t i = 0; i < 3; ++i) v[i] = parseNumber(parts[i]);
        if (parts.size() != 3 || !v[0] || !v[1] || !v[2])
            return usageError(io, "merge", "--offset expects three numbers x,y,z in mm, got '" + *text + "'.");
        request.offset = geometry::Transform::translation({*v[0], *v[1], *v[2]});
    }
    const std::string& targetFile = a.positional[0];
    const std::string out = a.value("o").value_or(targetFile);
    auto target = loadProjectOrReport(io, "merge", targetFile);
    if (!target) return kExitUsageOrIo;
    auto source = loadProjectOrReport(io, "merge", a.positional[1]);
    if (!source) return kExitUsageOrIo;
    auto merged = project::mergeProjects(target->project, source->project, request);
    if (!merged) {
        io.err << "fos merge: refused: " << merged.error().message << "\nNothing was saved.\n";
        return merged.error().code == core::ErrorCode::NotFound ? kExitUsageOrIo : kExitValidationErrors;
    }
    if (auto r = project::saveProject(pathFromUtf8(out), merged.value().project); !r) {
        io.err << "fos merge: cannot save '" << out << "': " << r.error().message << "\n";
        return kExitUsageOrIo;
    }
    const auto& plan = merged.value().plan;
    for (const auto& i : plan.items)
        io.out << "Imported " << i.sourceId << (i.renamed() ? " as " + i.newId : std::string()) << " ('" << i.name
               << "')\n";
    for (const auto& le : plan.library) {
        const auto& e = le.entry;
        if (e.action == formats::MergeAction::Skipped && !le.conflict) continue;
        io.out << "  " << formats::toString(e.category) << " " << e.incomingId << ": " << formats::toString(e.action)
               << (e.action == formats::MergeAction::Renamed ? " to " + e.resultId : std::string())
               << (le.conflict ? " (conflict: differs from the target's entry)" : "") << "\n";
    }
    io.out << "Saved " << out << "\n";
    return kExitOk;
}

} // namespace fos::cli::detail
