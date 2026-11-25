// SPDX-License-Identifier: AGPL-3.0-only
// fos edgeband-jobs: edge-banding job list for the banding machine operator.
#include "common.hpp"

#include "fos/production/csv.hpp"
#include "fos/production/edge_banding_jobs.hpp"

namespace fos::cli::detail {
namespace {

const char* kEdgebandJobsHelp =
    "Usage: fos edgeband-jobs <file.fosp> [--format text|csv|json] [--svg DIR] [--assembly ID]\n\n"
    "Edge-banding job list for the banding machine operator, grouped by edge band (one roll\n"
    "change per band): per part the cut size and, per banded edge (L1/L2/W1/W2), the cut edge\n"
    "length, the finished edge length, the strip length incl. the project's overhang per end and\n"
    "a SUGGESTED banding step (long edges first, then short edges, so the short-edge band\n"
    "overlaps the long-edge band ends: common practice, not a machine requirement). Totals per\n"
    "band. Shaped (contoured) parts are listed separately for contour banding (manual / edge\n"
    "bander with contour unit) with their strip and segment lengths.\n"
    "--format csv|json writes the list to stdout (diagnostics to stderr). --svg DIR writes one\n"
    "printable A4 sheet per band: DIR/edgeband-<band>[-<page>].svg. --assembly limits the list to\n"
    "one item. Exit code 1 when the project has model errors.\n";

} // namespace

int cmdEdgebandJobs(const std::vector<std::string>& args, Io& io) {
    constexpr const char* kCmd = "edgeband-jobs";
    auto parsed = parseArgs(args, {{"format"}, {"svg"}, {"assembly"}});
    if (!parsed) return usageError(io, kCmd, parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kEdgebandJobsHelp;
        return kExitOk;
    }
    if (a.positional.size() != 1) return usageError(io, kCmd, "expected exactly one project file.");
    const std::string format = a.value("format").value_or("text");
    if (format != "text" && format != "csv" && format != "json")
        return usageError(io, kCmd, "--format '" + format + "' is not one of text, csv, json.");
    production::Scope scope;
    if (const auto id = a.value("assembly")) scope.assembly = domain::AssemblyId(*id);

    auto loaded = loadProjectOrReport(io, kCmd, a.positional[0]);
    if (!loaded) return kExitUsageOrIo;
    const std::string incomplete = resolveErrorNotice(kCmd, *loaded);
    const auto jobs =
        production::edgeBandingJobs(loaded->resolved.model, loaded->project.settings.edgeBanding, scope);

    if (format == "text") {
        io.out << production::edgeBandingJobsText(jobs);
        if (!jobs.diagnostics.empty()) {
            io.out << "Diagnostics:\n";
            printDiagnostics(io.out, jobs.diagnostics);
        }
    } else {
        io.out << (format == "csv" ? production::toCsv(production::edgeBandingJobsTable(jobs))
                                   : production::edgeBandingJobsJson(jobs));
        printDiagnostics(io.err, jobs.diagnostics);
    }

    if (const auto dir = a.value("svg")) {
        const auto d = pathFromUtf8(*dir);
        if (!ensureOutputDirectory(io, kCmd, d)) return kExitUsageOrIo;
        std::ostream& log = format == "text" ? io.out : io.err;
        for (const auto& sheet : production::edgeBandingJobSheetsSvg(jobs)) {
            const std::string stem = "edgeband-" + safeFileName(sheet.edgeBandId.str());
            for (std::size_t i = 0; i < sheet.pages.size(); ++i) {
                const auto file =
                    d / (stem + (sheet.pages.size() > 1 ? "-" + std::to_string(i + 1) : std::string()) + ".svg");
                if (!writeOutputFile(io, kCmd, file, sheet.pages[i])) return kExitUsageOrIo;
                log << "wrote " << pathToUtf8(file) << "\n";
            }
        }
    }
    if (!incomplete.empty()) {
        io.err << incomplete;
        return kExitValidationErrors;
    }
    return errorCount(jobs.diagnostics) > 0 ? kExitValidationErrors : kExitOk;
}

} // namespace fos::cli::detail
