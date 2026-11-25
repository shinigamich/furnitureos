// SPDX-License-Identifier: AGPL-3.0-only
// `fos diff`: compare two projects (revisions).
#include "common.hpp"

#include "fos/cli/cli.hpp"
#include "fos/project/diff.hpp"

namespace fos::cli::detail {
namespace {

const char* kDiffHelp =
    "Usage: fos diff <a.fosp> <b.fosp> [--model] [--format text|json] [--ignore-timestamps]\n\n"
    "Compares project A with project B and prints what changed from A to B, grouped:\n"
    "metadata, library entries (added / removed / changed fields), items (added / removed /\n"
    "renamed; for changed items: parameters old -> new, materials, hardware, construction,\n"
    "interior and fronts as field paths, placement moved / rotated), settings (cutting,\n"
    "pricing, room, other) and other stored data. Numbers closer than 1e-6 are equal.\n"
    "--model also generates both projects and compares the parts by id (added / removed,\n"
    "finished size, material, edge bands, grain, operation count) and the hardware quantities:\n"
    "what changes in production. --format json prints a JSON document for tools.\n"
    "--ignore-timestamps does not compare the metadata createdAt / modifiedAt.\n\n"
    "Exit codes: 0 the projects are identical, 1 they differ, 2 usage error or a file that\n"
    "cannot be loaded (or, with --model, resolved).\n";

} // namespace

int cmdDiff(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"model", false}, {"format"}, {"ignore-timestamps", false}});
    if (!parsed) return usageError(io, "diff", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kDiffHelp;
        return kExitOk;
    }
    if (a.positional.size() != 2) return usageError(io, "diff", "expected <a.fosp> <b.fosp>.");
    const std::string format = a.value("format").value_or("text");
    if (format != "text" && format != "json")
        return usageError(io, "diff", "--format must be text or json, not '" + format + "'.");
    project::DiffOptions options;
    options.model = a.has("model");
    options.ignoreTimestamps = a.has("ignore-timestamps");

    auto before = loadProjectOrReport(io, "diff", a.positional[0]);
    if (!before) return kExitUsageOrIo;
    auto after = loadProjectOrReport(io, "diff", a.positional[1]);
    if (!after) return kExitUsageOrIo;
    auto diff = project::diffProjects(before->project, after->project, options);
    if (!diff) {
        io.err << "fos diff: " << diff.error().message << "\n";
        return kExitUsageOrIo;
    }
    if (format == "json") io.out << project::diffToJson(diff.value()).dump(2) << "\n";
    else io.out << project::diffText(diff.value());
    return diff.value().empty() ? kExitOk : kExitValidationErrors;
}

} // namespace fos::cli::detail
