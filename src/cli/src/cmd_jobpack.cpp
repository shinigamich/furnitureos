// SPDX-License-Identifier: AGPL-3.0-only
// fos jobpack  (one self-contained printable HTML for the workshop, fos/production/job_pack.hpp)
#include "common.hpp"

#include "fos/core/units.hpp"
#include "fos/cutting/svg.hpp"
#include "fos/nesting/svg.hpp"
#include "fos/production/job_pack.hpp"
#include "fos/project/outputs.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace fos::cli::detail {
namespace {

const char* kJobpackHelp =
    "Usage: fos jobpack <file.fosp> -o|--output <job.html> [--no-plans] [--sections a,b,...] [--date YYYY-MM-DD]\n\n"
    "Writes ONE self-contained, printable HTML document for the workshop (A4 landscape print CSS, every\n"
    "section on a new page, no external resources, no JavaScript):\n"
    "  cover        project, date, items with quantities, totals, contents\n"
    "  parts        part list\n"
    "  cutlist      cut list per material (saw sizes)\n"
    "  boughtin     bought-in items (glass panes ordered cut to size)\n"
    "  edgebanding  edge-banding jobs per band (edges, steps, strip lengths)\n"
    "  hardware     hardware list, with total lengths of linear fittings\n"
    "  packing      packing list (default limits as 'fos packing') and hardware bags\n"
    "  assembly     assembly sheets (GENERIC step order, not a verified instruction)\n"
    "  plans        cutting plans as inline SVG (HEURISTIC layouts, as 'fos cut')\n"
    "  warnings     warnings and errors of the reports above\n"
    "--sections picks a comma-separated subset (always printed in the order above); --no-plans leaves out\n"
    "the cutting plans (and skips the optimisation). --date is printed on the cover (default: today, UTC).\n"
    "Exit code 1 when the project has model errors or a cutting plan failed.\n";

std::string todayUtc() {
    const auto days = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
    const std::chrono::year_month_day ymd{days};
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u", static_cast<int>(ymd.year()), static_cast<unsigned>(ymd.month()),
                  static_cast<unsigned>(ymd.day()));
    return buf;
}

std::string pct(double v) { return core::formatLength(v, 1) + " %"; }

/// Drawings of every guillotine and nesting sheet, in model material order.
void addPlans(const project::CuttingPlans& plans, production::JobPackOptions& o) {
    o.planNotes.push_back("Layouts are produced by HEURISTIC optimisers (guillotine for rectangular parts, true-shape "
                          "nesting for shaped parts); they are in general not optimal. Piece labels as in 'fos cut'.");
    for (const auto& material : plans.materials) {
        if (material.guillotine) {
            const auto& plan = plans.plans[*material.guillotine];
            if (!plan.result) {
                o.planNotes.push_back("Material " + plan.job.name + " (" + plan.job.id +
                                      "): optimisation failed, see warnings.");
            } else {
                const auto& sheets = plan.result->sheets;
                for (std::size_t i = 0; i < sheets.size(); ++i)
                    o.cuttingPlans.push_back(
                        {plan.job.name + " (" + plan.job.id + ") - sheet " + std::to_string(i + 1) + " of " +
                             std::to_string(sheets.size()) + ": " + sheets[i].stockId + " " +
                             core::formatLength(sheets[i].sheetLength) + " x " + core::formatLength(sheets[i].sheetWidth) +
                             " mm, " + std::to_string(sheets[i].placements.size()) + " pieces, utilisation " +
                             pct(sheets[i].utilisationOfSheetPct),
                         cutting::renderSheetSvg(plan.job, sheets[i])});
            }
        }
        if (material.nesting) {
            const auto& plan = plans.nestingPlans[*material.nesting];
            const std::string name = plan.job.materialName + " (" + plan.job.materialId + ")";
            if (!plan.result) {
                o.planNotes.push_back("Material " + name + ": nesting of shaped parts failed, see warnings.");
            } else {
                const auto& sheets = plan.result->sheets;
                for (std::size_t i = 0; i < sheets.size(); ++i)
                    o.cuttingPlans.push_back({name + " - shaped parts, nest sheet " + std::to_string(i + 1) + " of " +
                                                  std::to_string(sheets.size()) + ": " + sheets[i].sheetId + ", " +
                                                  std::to_string(sheets[i].placements.size()) + " part(s)",
                                              nesting::renderNestSvg(plan.job.job, sheets[i])});
            }
        }
    }
}

} // namespace

int cmdJobpack(const std::vector<std::string>& args, Io& io) {
    // The shared parser knows only long options: "-o" is spelled "--output" for it.
    std::vector<std::string> normalised = args;
    for (auto& arg : normalised) {
        if (arg == "--") break;
        if (arg == "-o") arg = "--output";
    }
    auto parsed = parseArgs(normalised, {{"output"}, {"no-plans", false}, {"sections"}, {"date"}});
    if (!parsed) return usageError(io, "jobpack", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kJobpackHelp;
        return kExitOk;
    }
    if (a.positional.size() != 1) return usageError(io, "jobpack", "expected exactly one project file.");
    const auto output = a.value("output");
    if (!output || output->empty()) return usageError(io, "jobpack", "-o <job.html> is required.");

    production::JobPackOptions o;
    if (const auto list = a.value("sections")) {
        o.sections.clear();
        std::size_t start = 0;
        while (start <= list->size()) {
            const auto comma = list->find(',', start);
            const std::string name = list->substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            const auto s = production::jobPackSectionFromString(name);
            if (!s)
                return usageError(io, "jobpack",
                                  "unknown section '" + name +
                                      "' (cover, parts, cutlist, boughtin, edgebanding, hardware, packing, assembly, "
                                      "plans, warnings).");
            o.sections.push_back(*s);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }
    if (a.has("no-plans"))
        std::erase(o.sections, production::JobPackSection::Plans);
    if (o.sections.empty()) return usageError(io, "jobpack", "no section left to print.");
    const bool withPlans = std::find(o.sections.begin(), o.sections.end(), production::JobPackSection::Plans) !=
                           o.sections.end();

    auto loaded = loadProjectOrReport(io, "jobpack", a.positional[0]);
    if (!loaded) return kExitUsageOrIo;
    const auto settings = project::productionOptions(loaded->project);
    o.projectName = loaded->project.metadata.name;
    o.author = loaded->project.metadata.author;
    o.date = a.value("date").value_or(todayUtc());
    o.cutList = settings.cutList;
    o.edgeBanding = settings.edgeBanding;
    bool planErrors = false;
    if (withPlans) {
        const auto plans = project::cuttingPlans(loaded->resolved, loaded->project.cutting);
        addPlans(plans, o);
        o.extraDiagnostics = plans.diagnostics;
        planErrors = core::hasErrors(plans.diagnostics);
    }
    auto html = production::jobPackHtml(loaded->resolved.model, o);
    if (!html) return usageError(io, "jobpack", html.error().message);
    const auto path = pathFromUtf8(*output);
    if (!writeOutputFile(io, "jobpack", path, html.value())) return kExitUsageOrIo;
    io.out << "Wrote job pack of '" << o.projectName << "' (" << o.sections.size() << " section(s)"
           << (withPlans ? ", " + std::to_string(o.cuttingPlans.size()) + " cutting-plan sheet(s)" : std::string())
           << ") to " << pathToUtf8(path) << "\n";
    const std::string incomplete = resolveErrorNotice("jobpack", *loaded);
    io.err << incomplete;
    return planErrors || !incomplete.empty() ? kExitValidationErrors : kExitOk;
}

} // namespace fos::cli::detail
