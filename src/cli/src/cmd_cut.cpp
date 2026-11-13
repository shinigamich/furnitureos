// SPDX-License-Identifier: AGPL-3.0-only
// fos cut
#include "common.hpp"

#include "fos/core/units.hpp"
#include "fos/cutting/svg.hpp"
#include "fos/project/outputs.hpp"

#include <sstream>

namespace fos::cli::detail {
namespace {

const char* kCutHelp =
    "Usage: fos cut <file.fosp> [--out-dir DIR]\n\n"
    "Builds one cutting job per board material (saw sizes, kerf / trim / remnants from the\n"
    "project cutting settings), runs the guillotine cutting optimiser and validates every\n"
    "layout independently (cutting::validateLayout). Writes DIR/<material>-sheet-<n>.svg\n"
    "(1 SVG user unit = 1 mm) and DIR/cutting-summary.txt (also printed). DIR defaults to the\n"
    "current directory and is created if needed.\n"
    "The optimiser is a HEURISTIC: utilisation figures describe the layout found, which is in\n"
    "general not optimal. Exit code 1 when the validator or the optimiser reports an error.\n";

std::string pct(double v) { return core::formatLength(v, 2) + " %"; }
std::string m2(double mm2) { return core::formatLength(mm2 / 1e6, 4) + " m2"; }

} // namespace

int cmdCut(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"out-dir"}});
    if (!parsed) return usageError(io, "cut", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kCutHelp;
        return kExitOk;
    }
    if (a.positional.size() != 1) return usageError(io, "cut", "expected exactly one project file.");
    auto loaded = loadProjectOrReport(io, "cut", a.positional[0]);
    if (!loaded) return kExitUsageOrIo;
    const std::filesystem::path dir = a.value("out-dir").value_or(".");
    if (!ensureOutputDirectory(io, "cut", dir)) return kExitUsageOrIo;

    const auto plans = project::cuttingPlans(loaded->resolved, loaded->project.cutting);
    std::ostringstream s;
    s << "Cutting plans of '" << loaded->project.metadata.name << "'\n"
      << "Layouts are produced by a HEURISTIC optimiser; utilisation is that of the layout found, which is in "
         "general not optimal.\n";
    if (plans.plans.empty()) s << "No parts to cut.\n";
    std::vector<std::pair<std::filesystem::path, std::string>> files;
    for (const auto& plan : plans.plans) {
        const auto& job = plan.job;
        int required = 0;
        for (const auto& piece : job.pieces) required += piece.quantity;
        s << "\nMaterial " << job.id << " \"" << job.name << "\" (" << core::formatLength(job.thickness) << " mm)\n"
          << "  kerf " << core::formatLength(job.kerf) << " mm, trim left/right/bottom/top "
          << core::formatLength(job.trim.left) << "/" << core::formatLength(job.trim.right) << "/"
          << core::formatLength(job.trim.bottom) << "/" << core::formatLength(job.trim.top) << " mm\n";
        if (!plan.result) {
            s << "  optimisation failed (see diagnostics)\n";
            continue;
        }
        const auto& res = *plan.result;
        s << "  pieces: " << required << " required, " << res.totals.placedCount << " placed, "
          << res.totals.unplacedCount << " unplaced\n";
        for (std::size_t i = 0; i < res.sheets.size(); ++i) {
            const auto& sheet = res.sheets[i];
            const std::string name = safeFileName(job.id) + "-sheet-" + std::to_string(i + 1) + ".svg";
            files.emplace_back(dir / name, cutting::renderSheetSvg(job, sheet));
            s << "  sheet " << i + 1 << ": " << sheet.stockId << " " << core::formatLength(sheet.sheetLength) << " x "
              << core::formatLength(sheet.sheetWidth) << " mm, " << sheet.placements.size()
              << " pieces, utilisation " << pct(sheet.utilisationOfSheetPct) << " of the sheet ("
              << pct(sheet.utilisationOfUsablePct) << " of the usable area), " << sheet.cuts.size()
              << " cuts -> " << name << "\n";
        }
        s << "  total: " << res.totals.sheetCount << " sheet(s), placed area " << m2(res.totals.usedArea)
          << " of " << m2(res.totals.sheetArea) << " sheet area = utilisation " << pct(res.totals.utilisationOfSheetPct)
          << " (heuristic result)\n"
          << "  sheet count lower bound (area): " << res.sheetCountLowerBound
          << (res.isSheetCountProvenMinimal ? " - sheet count proven minimal" : " - sheet count not proven minimal")
          << "\n"
          << "  strategy: " << res.strategy << "\n";
        for (const auto& u : res.unplaced) s << "  UNPLACED: " << u.message << "\n";
        s << "  validator (cutting::validateLayout): "
          << (plan.validation.empty() ? std::string("no findings")
                                      : std::to_string(errorCount(plan.validation)) + " errors, " +
                                            std::to_string(plan.validation.size() - errorCount(plan.validation)) +
                                            " other findings")
          << "\n";
        std::ostringstream v;
        printDiagnostics(v, plan.validation, "    ");
        s << v.str();
    }
    s << "\nDiagnostics: " << (plans.diagnostics.empty() ? "none" : std::to_string(plans.diagnostics.size())) << "\n";
    std::ostringstream d;
    printDiagnostics(d, plans.diagnostics);
    s << d.str();

    for (const auto& [path, content] : files)
        if (!writeOutputFile(io, "cut", path, content)) return kExitUsageOrIo;
    const std::string summary = s.str();
    if (!writeOutputFile(io, "cut", dir / "cutting-summary.txt", summary)) return kExitUsageOrIo;
    io.out << summary << "Wrote " << files.size() << " sheet SVG(s) and cutting-summary.txt to " << dir.string() << "\n";
    return core::hasErrors(plans.diagnostics) ? kExitValidationErrors : kExitOk;
}

} // namespace fos::cli::detail
