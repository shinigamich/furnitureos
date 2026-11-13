// SPDX-License-Identifier: AGPL-3.0-only
// fos report / labels / export-obj
#include "common.hpp"

#include "fos/formats/obj_export.hpp"
#include "fos/production/csv.hpp"
#include "fos/production/html.hpp"
#include "fos/production/labels.hpp"
#include "fos/production/tables.hpp"
#include "fos/project/outputs.hpp"

namespace fos::cli::detail {
namespace {

const char* kReportHelp =
    "Usage: fos report <file.fosp> --type parts|cutlist|materials|edgebanding|hardware|machining|summary\n"
    "                  [--csv out.csv] [--separator ';']\n"
    "       fos report <file.fosp> --html out.html <item-id>\n\n"
    "Production reports of the resolved model. Without --csv the report is printed as a text\n"
    "table followed by its diagnostics. --csv writes RFC 4180 CSV (UTF-8, CRLF) with the given\n"
    "one-character separator (default ','). --html writes the printable production sheet of one\n"
    "item (part list, cut list, edge banding, hardware, warnings).\n"
    "Cut sizes are saw sizes (finished size minus edge banding). Material sheet counts in the\n"
    "materials report are AREA LOWER BOUNDS; real sheet counts come from 'fos cut'.\n";

const char* kLabelsHelp =
    "Usage: fos labels <file.fosp> --svg-dir DIR\n\n"
    "Writes one label per part instance on A4 pages (70 x 37 mm labels, 3 x 8 per page) as\n"
    "DIR/labels-1.svg, DIR/labels-2.svg, ... (1 SVG user unit = 1 mm). DIR is created if needed.\n";

const char* kObjHelp =
    "Usage: fos export-obj <file.fosp> <out.obj>\n\n"
    "Writes the resolved model as Wavefront OBJ (every part a box at its finished size, project\n"
    "frame, mm, Z up) and the companion <out>.mtl with one material per board material.\n"
    "Not exported: machining, edge bands, hardware, textures.\n";

struct ReportTable {
    production::Table table;
    core::Diagnostics diagnostics;
};

std::optional<ReportTable> reportTable(const std::string& type, const project::ProductionReports& r) {
    if (type == "parts") return ReportTable{production::partListTable(r.parts), r.parts.diagnostics};
    if (type == "cutlist") return ReportTable{production::cutListTable(r.cutList), r.cutList.diagnostics};
    if (type == "materials") return ReportTable{production::materialTable(r.materials), r.materials.diagnostics};
    if (type == "edgebanding")
        return ReportTable{production::edgeBandingTable(r.edgeBanding), r.edgeBanding.diagnostics};
    if (type == "hardware") return ReportTable{production::hardwareTable(r.hardware), r.hardware.diagnostics};
    if (type == "machining") return ReportTable{production::machiningTable(r.machining), r.machining.diagnostics};
    if (type == "summary") return ReportTable{production::summaryTable(r.summary), {}};
    return std::nullopt;
}

bool ensureDirectory(Io& io, std::string_view command, const std::filesystem::path& dir) {
    std::error_code ec;
    if (std::filesystem::is_directory(dir, ec)) return true;
    std::filesystem::create_directories(dir, ec);
    if (ec || !std::filesystem::is_directory(dir)) {
        io.err << "fos " << command << ": cannot create directory '" << dir.string() << "': " << ec.message() << "\n";
        return false;
    }
    return true;
}

} // namespace

bool ensureOutputDirectory(Io& io, std::string_view command, const std::filesystem::path& dir) {
    return ensureDirectory(io, command, dir);
}

int cmdReport(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"type"}, {"csv"}, {"separator"}, {"html"}});
    if (!parsed) return usageError(io, "report", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kReportHelp;
        return kExitOk;
    }
    if (a.positional.empty()) return usageError(io, "report", "expected a project file.");
    const bool html = a.has("html");
    if (html && (a.has("type") || a.has("csv") || a.has("separator")))
        return usageError(io, "report", "--html cannot be combined with --type, --csv or --separator.");
    if (html && a.positional.size() != 2) return usageError(io, "report", "--html needs exactly one item id.");
    if (!html && a.positional.size() != 1) return usageError(io, "report", "unexpected argument '" + a.positional.back() + "'.");
    if (!html && !a.has("type")) return usageError(io, "report", "--type is required (or --html <item>).");
    char separator = ',';
    if (auto s = a.value("separator")) {
        if (!a.has("csv")) return usageError(io, "report", "--separator needs --csv.");
        if (s->size() != 1 || (*s)[0] == '"' || (*s)[0] == '\r' || (*s)[0] == '\n')
            return usageError(io, "report", "--separator must be one character other than a quote or line break.");
        separator = (*s)[0];
    }
    const std::string type = a.value("type").value_or("");
    if (!html && type != "parts" && type != "cutlist" && type != "materials" && type != "edgebanding" &&
        type != "hardware" && type != "machining" && type != "summary")
        return usageError(io, "report", "unknown report type '" + type +
                                            "' (parts, cutlist, materials, edgebanding, hardware, machining, summary).");

    auto loaded = loadProjectOrReport(io, "report", a.positional[0]);
    if (!loaded) return kExitUsageOrIo;
    const auto& project = loaded->project;
    const auto& resolved = loaded->resolved;

    if (html) {
        const std::string& itemId = a.positional[1];
        if (!project.findItem(itemId)) return usageError(io, "report", "no item '" + itemId + "' in the project.");
        production::HtmlSheetOptions options;
        options.projectName = project.metadata.name;
        options.edgeBanding = project.settings.edgeBanding;
        options.cutList.groupAcrossAssemblies = project.settings.cutListGroupAcrossAssemblies;
        auto sheet = production::productionSheetHtml(resolved.model, domain::AssemblyId(itemId), options);
        if (!sheet) {
            io.err << "fos report: " << sheet.error().message << "\n";
            return kExitValidationErrors; // the item exists but was not generated (see fos info)
        }
        if (!writeOutputFile(io, "report", *a.value("html"), sheet.value())) return kExitUsageOrIo;
        io.out << "Wrote production sheet of " << itemId << " to " << *a.value("html") << "\n";
        return kExitOk;
    }

    const auto reports = project::productionReports(resolved, project::productionOptions(project));
    const auto table = reportTable(type, reports);
    if (auto csvPath = a.value("csv")) {
        production::CsvOptions csv;
        csv.separator = separator;
        if (!writeOutputFile(io, "report", *csvPath, production::toCsv(table->table, csv))) return kExitUsageOrIo;
        io.out << "Wrote " << type << " report (" << table->table.rows.size() << " rows) to " << *csvPath << "\n";
    } else {
        io.out << textTable(table->table);
        if (type == "summary")
            io.out << "Total: " << reports.summary.partCount << " parts, " << reports.summary.hardwareQuantity
                   << " hardware items, " << reports.summary.operationCount << " operations, finished area "
                   << core::formatLength(reports.summary.finishedAreaM2, 4) << " m2, cut area "
                   << core::formatLength(reports.summary.cutAreaM2, 4) << " m2, edge banding "
                   << core::formatLength(reports.summary.edgeBandingAppliedM, 3) << " m\n";
        if (type == "materials")
            io.out << "Note: sheet counts are area lower bounds; run 'fos cut' for a cutting plan.\n";
    }
    if (!table->diagnostics.empty()) {
        io.out << "Diagnostics:\n";
        printDiagnostics(io.out, table->diagnostics);
    }
    return core::hasErrors(table->diagnostics) ? kExitValidationErrors : kExitOk;
}

int cmdLabels(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"svg-dir"}});
    if (!parsed) return usageError(io, "labels", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kLabelsHelp;
        return kExitOk;
    }
    if (a.positional.size() != 1) return usageError(io, "labels", "expected exactly one project file.");
    if (!a.has("svg-dir")) return usageError(io, "labels", "--svg-dir is required.");
    auto loaded = loadProjectOrReport(io, "labels", a.positional[0]);
    if (!loaded) return kExitUsageOrIo;
    production::LabelOptions options;
    options.projectName = loaded->project.metadata.name;
    options.cutList.groupAcrossAssemblies = loaded->project.settings.cutListGroupAcrossAssemblies;
    const auto set = production::labels(loaded->resolved.model, options);
    auto pages = production::renderLabelSheetsSvg(set.labels);
    if (!pages) {
        io.err << "fos labels: " << pages.error().message << "\n";
        return kExitUsageOrIo;
    }
    const std::filesystem::path dir = *a.value("svg-dir");
    if (!ensureDirectory(io, "labels", dir)) return kExitUsageOrIo;
    for (std::size_t i = 0; i < pages.value().size(); ++i) {
        const auto file = dir / ("labels-" + std::to_string(i + 1) + ".svg");
        if (!writeOutputFile(io, "labels", file, pages.value()[i])) return kExitUsageOrIo;
    }
    io.out << "Wrote " << set.labels.size() << " labels on " << pages.value().size() << " page(s) to " << dir.string()
           << "\n";
    if (!set.diagnostics.empty()) {
        io.out << "Diagnostics:\n";
        printDiagnostics(io.out, set.diagnostics);
    }
    return core::hasErrors(set.diagnostics) ? kExitValidationErrors : kExitOk;
}

int cmdExportObj(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {});
    if (!parsed) return usageError(io, "export-obj", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kObjHelp;
        return kExitOk;
    }
    if (a.positional.size() != 2) return usageError(io, "export-obj", "expected <file.fosp> <out.obj>.");
    auto loaded = loadProjectOrReport(io, "export-obj", a.positional[0]);
    if (!loaded) return kExitUsageOrIo;
    const std::filesystem::path out = a.positional[1];
    std::error_code ec;
    if (!out.parent_path().empty() && !std::filesystem::is_directory(out.parent_path(), ec))
        return usageError(io, "export-obj", "directory '" + out.parent_path().string() + "' does not exist.");
    auto r = formats::writeObjFiles(out, loaded->resolved.model);
    if (!r) {
        io.err << "fos export-obj: " << r.error().message << "\n";
        return r.error().code == core::ErrorCode::IoError ? kExitUsageOrIo : kExitValidationErrors;
    }
    std::size_t parts = 0;
    for (const auto& asmb : loaded->resolved.model.assemblies) parts += asmb.parts.size();
    io.out << "Wrote " << parts << " parts to " << out.string() << " (+ MTL beside it)\n";
    if (!r.value().diagnostics.empty()) printDiagnostics(io.out, r.value().diagnostics);
    return kExitOk;
}

} // namespace fos::cli::detail
