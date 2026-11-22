// SPDX-License-Identifier: AGPL-3.0-only
// fos cut
#include "common.hpp"

#include "fos/core/units.hpp"
#include "fos/cutting/svg.hpp"
#include "fos/nesting/svg.hpp"
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
    "current directory and is created if needed. SHAPED parts (arcs, cut-outs) are not sawn: they\n"
    "are nested true-shape for routing (project nesting settings: tool diameter, clearance, chord\n"
    "tolerance, margins, iterations, seed), validated independently (nesting::validateNest) and\n"
    "written as DIR/<material>-nest-<n>.svg. Both optimisers are HEURISTICS.\n"
    "The optimisers are HEURISTICS: utilisation figures describe the layout found, which is in\n"
    "general not optimal. Exit code 1 when the validator or the optimiser reports an error, or\n"
    "when the project has model errors (e.g. an item that cannot be generated: its parts are\n"
    "missing from the plans; the summary and stderr say which items).\n";

std::string pct(double v) {
    return core::formatLength(v, 2) + " %";
}
std::string m2(double mm2) {
    return core::formatLength(mm2 / 1e6, 4) + " m2";
}

std::string findings(const core::Diagnostics& d) {
    return d.empty() ? std::string("no findings")
                     : std::to_string(errorCount(d)) + " errors, " +
                           std::to_string(d.size() - errorCount(d)) + " other findings";
}

void printNesting(std::ostream& s, const project::NestingPlan& plan, const project::NestingSettings& ns,
                  const std::filesystem::path& dir,
                  std::vector<std::pair<std::filesystem::path, std::string>>& files) {
    const auto& job = plan.job.job;
    const auto& m = job.sheets.empty() ? nesting::Margins{} : job.sheets.front().margins;
    s << "  SHAPED parts (routed, true-shape nesting HEURISTIC - not optimal): " << job.parts.size()
      << " part(s)\n"
      << "    tool " << core::formatLength(ns.toolDiameter) << " mm + clearance "
      << core::formatLength(ns.clearance) << " mm, chord tolerance "
      << core::formatLength(ns.chordTolerance, 3) << " mm -> part spacing "
      << core::formatLength(job.partSpacing, 3) << " mm, edge spacing "
      << core::formatLength(job.edgeSpacing, 3) << " mm; margins left/right/bottom/top "
      << core::formatLength(m.left) << "/" << core::formatLength(m.right) << "/"
      << core::formatLength(m.bottom) << "/" << core::formatLength(m.top) << " mm; " << ns.iterations
      << " iterations, seed " << ns.seed << "\n";
    if (!plan.result) {
        s << "    nesting failed (see diagnostics)\n";
        return;
    }
    const auto& res = *plan.result;
    s << "    parts: " << job.parts.size() << " required, " << res.placedCount << " placed, "
      << res.unplaced.size() << " unplaced\n";
    for (std::size_t i = 0; i < res.sheets.size(); ++i) {
        const auto& sheet = res.sheets[i];
        const std::string name =
            safeFileName(plan.job.materialId) + "-nest-" + std::to_string(i + 1) + ".svg";
        files.emplace_back(dir / name, nesting::renderNestSvg(job, sheet));
        s << "    nest sheet " << i + 1 << ": " << sheet.sheetId << ", " << sheet.placements.size()
          << " part(s), used length " << core::formatLength(sheet.usedLength) << " mm, utilisation "
          << pct(100.0 * sheet.utilisation) << " of the sheet -> " << name << "\n";
    }
    s << "    total: " << res.sheets.size() << " sheet(s), part area " << m2(res.totalPartArea)
      << " (net of cut-outs " << m2(res.totalNetPartArea) << ") of " << m2(res.totalSheetArea)
      << " = utilisation " << pct(100.0 * res.utilisation) << " (heuristic result, " << res.iterationsRun
      << " improvement iterations)\n";
    for (const auto& u : res.unplaced)
        s << "    UNPLACED: " << u.message << "\n";
    s << "    validator (nesting::validateNest): " << findings(plan.validation) << "\n";
    std::ostringstream v;
    printDiagnostics(v, plan.validation, "      ");
    s << v.str();
}

} // namespace

std::string cuttingPlansText(const project::CuttingPlans& plans, const project::NestingSettings& nestingSettings,
                             const std::filesystem::path& dir,
                             std::vector<std::pair<std::filesystem::path, std::string>>& files, std::size_t& nestFiles) {
    std::ostringstream s;
    if (plans.materials.empty())
        s << "No parts to cut.\n";
    if (!plans.nestingPlans.empty())
        s << "Shaped parts are nested by a true-shape HEURISTIC for routing; those layouts are not optimal "
             "either.\n";
    for (const auto& material : plans.materials) {
        if (!material.guillotine) {
            const auto& nj = plans.nestingPlans[*material.nesting].job;
            s << "\nMaterial " << nj.materialId << " \"" << nj.materialName << "\" ("
              << core::formatLength(nj.thickness) << " mm)\n";
        } else {
            const auto& plan = plans.plans[*material.guillotine];
            const auto& job = plan.job;
            int required = 0;
            for (const auto& piece : job.pieces)
                required += piece.quantity;
            s << "\nMaterial " << job.id << " \"" << job.name << "\" (" << core::formatLength(job.thickness)
              << " mm)\n"
              << "  kerf " << core::formatLength(job.kerf) << " mm, trim left/right/bottom/top "
              << core::formatLength(job.trim.left) << "/" << core::formatLength(job.trim.right) << "/"
              << core::formatLength(job.trim.bottom) << "/" << core::formatLength(job.trim.top) << " mm\n";
            if (!plan.result) {
                s << "  optimisation failed (see diagnostics)\n";
            } else {
                const auto& res = *plan.result;
                s << "  pieces: " << required << " required, " << res.totals.placedCount << " placed, "
                  << res.totals.unplacedCount << " unplaced\n";
                for (std::size_t i = 0; i < res.sheets.size(); ++i) {
                    const auto& sheet = res.sheets[i];
                    const std::string name =
                        safeFileName(job.id) + "-sheet-" + std::to_string(i + 1) + ".svg";
                    files.emplace_back(dir / name, cutting::renderSheetSvg(job, sheet));
                    s << "  sheet " << i + 1 << ": " << sheet.stockId << " "
                      << core::formatLength(sheet.sheetLength) << " x "
                      << core::formatLength(sheet.sheetWidth) << " mm, " << sheet.placements.size()
                      << " pieces, utilisation " << pct(sheet.utilisationOfSheetPct) << " of the sheet ("
                      << pct(sheet.utilisationOfUsablePct) << " of the usable area), " << sheet.cuts.size()
                      << " cuts -> " << name << "\n";
                }
                s << "  total: " << res.totals.sheetCount << " sheet(s), placed area "
                  << m2(res.totals.usedArea) << " of " << m2(res.totals.sheetArea)
                  << " sheet area = utilisation " << pct(res.totals.utilisationOfSheetPct)
                  << " (heuristic result)\n"
                  << "  sheet count lower bound (area): " << res.sheetCountLowerBound
                  << (res.isSheetCountProvenMinimal ? " - sheet count proven minimal"
                                                    : " - sheet count not proven minimal")
                  << "\n"
                  << "  strategy: " << res.strategy << "\n";
                for (const auto& u : res.unplaced)
                    s << "  UNPLACED: " << u.message << "\n";
                s << "  validator (cutting::validateLayout): " << findings(plan.validation) << "\n";
                std::ostringstream v;
                printDiagnostics(v, plan.validation, "    ");
                s << v.str();
            }
        }
        if (material.nesting) {
            const std::size_t before = files.size();
            printNesting(s, plans.nestingPlans[*material.nesting], nestingSettings, dir, files);
            nestFiles += files.size() - before;
        }
    }
    s << "\nDiagnostics: " << (plans.diagnostics.empty() ? "none" : std::to_string(plans.diagnostics.size()))
      << "\n";
    std::ostringstream d;
    printDiagnostics(d, plans.diagnostics);
    s << d.str();
    return s.str();
}

int cmdCut(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"out-dir"}});
    if (!parsed)
        return usageError(io, "cut", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kCutHelp;
        return kExitOk;
    }
    if (a.positional.size() != 1)
        return usageError(io, "cut", "expected exactly one project file.");
    auto loaded = loadProjectOrReport(io, "cut", a.positional[0]);
    if (!loaded)
        return kExitUsageOrIo;
    const std::filesystem::path dir = pathFromUtf8(a.value("out-dir").value_or("."));
    if (!ensureOutputDirectory(io, "cut", dir))
        return kExitUsageOrIo;

    const auto plans = project::cuttingPlans(loaded->resolved, loaded->project.cutting);
    std::ostringstream s;
    s << "Cutting plans of '" << loaded->project.metadata.name << "'\n"
      << "Layouts are produced by a HEURISTIC optimiser; utilisation is that of the layout found, which is "
         "in "
         "general not optimal.\n";
    const std::string incomplete = resolveErrorNotice("cut", *loaded);
    if (!incomplete.empty())
        s << incomplete;
    std::vector<std::pair<std::filesystem::path, std::string>> files;
    std::size_t nestFiles = 0;
    s << cuttingPlansText(plans, loaded->project.cutting.nestingSettings(), dir, files, nestFiles);

    for (const auto& [path, content] : files)
        if (!writeOutputFile(io, "cut", path, content))
            return kExitUsageOrIo;
    const std::string summary = s.str();
    if (!writeOutputFile(io, "cut", dir / "cutting-summary.txt", summary))
        return kExitUsageOrIo;
    io.out << summary << "Wrote " << files.size() - nestFiles << " sheet SVG(s) and "
           << (nestFiles == 0 ? std::string() : std::to_string(nestFiles) + " nesting sheet SVG(s) and ")
           << "cutting-summary.txt to " << pathToUtf8(dir) << "\n";
    io.err << incomplete;
    return core::hasErrors(plans.diagnostics) || !incomplete.empty() ? kExitValidationErrors : kExitOk;
}

} // namespace fos::cli::detail
