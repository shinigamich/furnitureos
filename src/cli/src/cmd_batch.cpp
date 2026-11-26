// SPDX-License-Identifier: AGPL-3.0-only
// fos batch new|add|report|cut|labels|quote  (production batches, fos/project/batch.hpp)
#include "common.hpp"

#include "fos/core/units.hpp"
#include "fos/production/csv.hpp"
#include "fos/project/batch.hpp"

#include <sstream>

namespace fos::cli::detail {
namespace {

const char* kBatchHelp =
    "Usage: fos batch <subcommand> ...\n\n"
    "Production batches: several projects (orders) produced together (.fosbatch files).\n\n"
    "  fos batch new <file.fosbatch> --name N [--date D] [--notes T]\n"
    "      Create an empty batch (refuses to overwrite an existing file).\n"
    "  fos batch add <file.fosbatch> <project.fosp> [--qty K] [--key KEY] [--note T]\n"
    "      Add a project K times (default 1); the path is stored relative to the batch file.\n"
    "      Adding a project that is already listed adds K to its quantity.\n"
    "  fos batch report <file.fosbatch> --type cutlist|summary|hardware|boughtin [--csv]\n"
    "      Combined cut list (part ids '<project key>/<part id>', '<key>#<n>/...' for copies),\n"
    "      per-project + total summary, hardware totals, or bought-in items (glass panes ordered\n"
    "      cut to size, all copies; not in the cut list).\n"
    "  fos batch cut <file.fosbatch> [--out-dir DIR]\n"
    "      Combined cutting plans (guillotine HEURISTIC) and nesting plans (shaped parts, true-shape\n"
    "      HEURISTIC) with the batch cutting settings; SVGs + cutting-summary.txt as 'fos cut'.\n"
    "  fos batch labels <file.fosbatch> --svg-dir DIR [--barcode none|code128]\n"
    "                   [--barcode-content part-id|project-part-id]\n"
    "      Part labels naming batch, project and order copy on each part.\n"
    "  fos batch quote <file.fosbatch> [--format text|csv|json] [--sheets]\n"
    "      Quote of the merged batch (all copies) as 'fos quote'. Pricing settings and currency are\n"
    "      those of the FIRST included project; a project with different ones is a warning.\n\n"
    "Every listed project is loaded on its own: a missing or broken project, a library conflict\n"
    "(same material id with different data) or a refused material substitution is reported and\n"
    "the other projects are still produced; the command then exits 1 (INCOMPLETE BATCH).\n";

using Files = std::vector<std::pair<std::filesystem::path, std::string>>;

struct Merged {
    project::LoadedBatch loaded;
    project::BatchModel model;
};

std::optional<Merged> loadMerged(Io& io, std::string_view command, const std::string& file) {
    auto lb = project::loadBatch(pathFromUtf8(file));
    if (!lb) {
        io.err << "fos " << command << ": cannot load batch '" << file << "': " << lb.error().message << "\n";
        return std::nullopt;
    }
    Merged m{std::move(lb.value()), {}};
    m.model = project::mergeBatch(m.loaded);
    return m;
}

/// Projects of the batch, then (when the merge has errors) an INCOMPLETE BATCH notice.
std::string batchHeader(const Merged& m, bool& incomplete) {
    std::ostringstream s;
    s << "Batch '" << m.loaded.batch.name << "'" << (m.loaded.batch.date.empty() ? "" : " (" + m.loaded.batch.date + ")")
      << ": " << m.model.projects.size() << " project(s)\n";
    for (const auto& p : m.model.projects)
      s << "  " << p.key << "  \"" << p.name << "\" x " << p.quantity << (p.included ? "" : "  [LEFT OUT]")
        << (p.note.empty() ? "" : "  - " + p.note) << "\n";
    incomplete = core::hasErrors(m.model.diagnostics);
    if (incomplete) s << "INCOMPLETE BATCH: the outputs below miss the projects marked [LEFT OUT] or are affected by errors:\n";
    if (!m.model.diagnostics.empty()) {
        std::ostringstream d;
        printDiagnostics(d, m.model.diagnostics);
        s << d.str();
    }
    return s.str();
}

int batchNew(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"name"}, {"date"}, {"notes"}});
    if (!parsed) return usageError(io, "batch new", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.positional.size() != 1) return usageError(io, "batch new", "expected exactly one batch file.");
    if (!a.has("name")) return usageError(io, "batch new", "--name is required.");
    const std::filesystem::path path = pathFromUtf8(a.positional[0]);
    std::error_code ec;
    if (std::filesystem::exists(path, ec))
        return usageError(io, "batch new", "'" + a.positional[0] + "' already exists; refusing to overwrite it.");
    project::Batch b;
    b.name = *a.value("name");
    b.date = a.value("date").value_or("");
    b.notes = a.value("notes").value_or("");
    formats::SaveOptions so;
    so.backupCount = 0;
    if (auto r = project::saveBatch(path, b, so); !r) {
        io.err << "fos batch new: " << r.error().message << "\n";
        return kExitUsageOrIo;
    }
    io.out << "Created batch '" << b.name << "' in " << a.positional[0] << "\n";
    return kExitOk;
}

int batchAdd(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"qty"}, {"key"}, {"note"}});
    if (!parsed) return usageError(io, "batch add", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.positional.size() != 2) return usageError(io, "batch add", "expected <file.fosbatch> <project.fosp>.");
    int qty = 1;
    if (const auto v = a.value("qty")) {
        const auto n = parseNumber(*v);
        if (!n || *n != static_cast<double>(static_cast<long long>(*n)) || *n < 1 || *n > project::kMaxBatchQuantity)
            return usageError(io, "batch add",
                              "--qty must be a whole number from 1 to " + std::to_string(project::kMaxBatchQuantity) +
                                  ", got '" + *v + "'.");
        qty = static_cast<int>(*n);
    }
    const std::filesystem::path batchPath = pathFromUtf8(a.positional[0]);
    auto batch = project::loadBatchDocument(batchPath);
    if (!batch) {
        io.err << "fos batch add: cannot load batch '" << a.positional[0] << "': " << batch.error().message << "\n";
        return kExitUsageOrIo;
    }
    const std::filesystem::path projectPath = pathFromUtf8(a.positional[1]);
    auto loaded = project::loadProject(projectPath);
    if (!loaded) {
        io.err << "fos batch add: cannot load project '" << a.positional[1] << "': " << loaded.error().message << "\n";
        return kExitUsageOrIo;
    }
    std::error_code ec;
    const auto base = std::filesystem::absolute(batchPath, ec).parent_path().lexically_normal();
    const auto abs = std::filesystem::absolute(projectPath, ec).lexically_normal();
    std::filesystem::path rel = abs.lexically_relative(base);
    if (rel.empty()) rel = abs; // e.g. another drive on Windows
    const std::u8string u = rel.generic_u8string();
    const std::string stored(u.begin(), u.end());

    project::Batch b = std::move(batch.value());
    project::BatchEntry* entry = nullptr;
    for (auto& e : b.entries)
        if (e.path == stored) entry = &e;
    if (entry != nullptr) {
        entry->quantity += qty;
        if (a.has("note")) entry->note = *a.value("note");
    } else {
        b.entries.push_back({stored, qty, a.value("key").value_or(""), a.value("note").value_or("")});
        entry = &b.entries.back();
    }
    formats::SaveOptions so;
    so.backupCount = 0;
    if (auto r = project::saveBatch(batchPath, b, so); !r) {
        io.err << "fos batch add: " << r.error().message << "\n";
        return r.error().code == core::ErrorCode::IoError ? kExitUsageOrIo : kExitValidationErrors;
    }
    io.out << "Batch '" << b.name << "': project '" << stored << "' (\"" << loaded.value().project.metadata.name
           << "\", key " << project::batchKey(*entry) << ") quantity " << entry->quantity << "\n";
    return kExitOk;
}

int batchReport(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"type"}, {"csv", false}});
    if (!parsed) return usageError(io, "batch report", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.positional.size() != 1) return usageError(io, "batch report", "expected exactly one batch file.");
    const std::string type = a.value("type").value_or("");
    if (type != "cutlist" && type != "summary" && type != "hardware" && type != "boughtin")
        return usageError(io, "batch report", "--type must be cutlist, summary, hardware or boughtin.");
    auto m = loadMerged(io, "batch report", a.positional[0]);
    if (!m) return kExitUsageOrIo;
    const project::Batch& batch = m->loaded.batch;
    production::Table table;
    core::Diagnostics reportDiagnostics;
    if (type == "cutlist") {
        const auto list = project::batchCutList(m->model, batch);
        table = production::cutListTable(list);
        reportDiagnostics = list.diagnostics;
    } else if (type == "boughtin") {
        const auto list = production::boughtInList(m->model.model);
        table = production::boughtInTable(list);
        reportDiagnostics = list.diagnostics;
    } else if (type == "hardware") {
        const auto hw = production::hardwareReport(m->model.model);
        table = production::hardwareTable(hw);
        reportDiagnostics = hw.diagnostics;
    } else {
        const auto s = project::batchSummary(m->model, batch);
        table = project::batchSummaryTable(s);
        reportDiagnostics = s.diagnostics;
    }
    bool incomplete = false;
    const std::string header = batchHeader(*m, incomplete);
    if (a.has("csv")) {
        io.out << production::toCsv(table);
        io.err << header;
    } else {
        io.out << header << "\n" << textTable(table);
        if (type == "summary")
            io.out << "Sheets are AREA LOWER BOUNDS, not a cutting plan ('fos batch cut' optimises the sheets); the "
                      "TOTAL row is computed on the merged batch, so orders sharing sheets can need fewer.\n";
    }
    if (!reportDiagnostics.empty()) {
        std::ostream& o = a.has("csv") ? io.err : io.out;
        o << "Diagnostics:\n";
        printDiagnostics(o, reportDiagnostics);
    }
    return incomplete || core::hasErrors(reportDiagnostics) ? kExitValidationErrors : kExitOk;
}

int batchCut(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"out-dir"}});
    if (!parsed) return usageError(io, "batch cut", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.positional.size() != 1) return usageError(io, "batch cut", "expected exactly one batch file.");
    auto m = loadMerged(io, "batch cut", a.positional[0]);
    if (!m) return kExitUsageOrIo;
    const std::filesystem::path dir = pathFromUtf8(a.value("out-dir").value_or("."));
    if (!ensureOutputDirectory(io, "batch cut", dir)) return kExitUsageOrIo;
    const auto plans = project::batchCuttingPlans(m->model, m->loaded.batch);
    bool incomplete = false;
    std::ostringstream s;
    s << "Cutting plans of batch '" << m->loaded.batch.name << "'\n"
      << "Layouts are produced by a HEURISTIC optimiser; utilisation is that of the layout found, which is in "
         "general not optimal.\n"
      << batchHeader(*m, incomplete);
    Files files;
    std::size_t nestFiles = 0;
    s << cuttingPlansText(plans, m->loaded.batch.cutting.nestingSettings(), dir, files, nestFiles);
    for (const auto& [path, content] : files)
        if (!writeOutputFile(io, "batch cut", path, content)) return kExitUsageOrIo;
    const std::string summary = s.str();
    if (!writeOutputFile(io, "batch cut", dir / "cutting-summary.txt", summary)) return kExitUsageOrIo;
    io.out << summary << "Wrote " << files.size() - nestFiles << " sheet SVG(s), " << nestFiles
           << " nesting sheet SVG(s) and cutting-summary.txt to " << pathToUtf8(dir) << "\n";
    return incomplete || core::hasErrors(plans.diagnostics) ? kExitValidationErrors : kExitOk;
}

int batchLabels(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"svg-dir"}, {"barcode"}, {"barcode-content"}});
    if (!parsed) return usageError(io, "batch labels", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.positional.size() != 1) return usageError(io, "batch labels", "expected exactly one batch file.");
    if (!a.has("svg-dir")) return usageError(io, "batch labels", "--svg-dir is required.");
    production::LabelSheetOptions sheet;
    if (const auto bad = applyLabelBarcodeOptions(a, sheet)) return usageError(io, "batch labels", *bad);
    auto m = loadMerged(io, "batch labels", a.positional[0]);
    if (!m) return kExitUsageOrIo;
    auto set = project::batchLabels(m->model, m->loaded.batch);
    auto sheets = production::renderLabelSheets(set.labels, sheet);
    if (!sheets) {
        io.err << "fos batch labels: " << sheets.error().message << "\n";
        return kExitUsageOrIo;
    }
    const auto& pages = sheets.value().pages;
    set.diagnostics.insert(set.diagnostics.end(), sheets.value().diagnostics.begin(), sheets.value().diagnostics.end());
    const std::filesystem::path dir = pathFromUtf8(*a.value("svg-dir"));
    if (!ensureOutputDirectory(io, "batch labels", dir)) return kExitUsageOrIo;
    for (std::size_t i = 0; i < pages.size(); ++i)
        if (!writeOutputFile(io, "batch labels", dir / ("labels-" + std::to_string(i + 1) + ".svg"), pages[i]))
            return kExitUsageOrIo;
    bool incomplete = false;
    io.out << batchHeader(*m, incomplete) << "Wrote " << set.labels.size() << " labels on " << pages.size()
           << " page(s) to " << pathToUtf8(dir) << "\n";
    if (!set.diagnostics.empty()) {
        io.out << "Diagnostics:\n";
        printDiagnostics(io.out, set.diagnostics);
    }
    return incomplete || core::hasErrors(set.diagnostics) ? kExitValidationErrors : kExitOk;
}

int batchQuote(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"format"}, {"sheets", false}});
    if (!parsed) return usageError(io, "batch quote", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.positional.size() != 1) return usageError(io, "batch quote", "expected exactly one batch file.");
    const auto format = quoteFormat(a);
    if (!format) return usageError(io, "batch quote", "--format must be text, csv or json.");
    auto m = loadMerged(io, "batch quote", a.positional[0]);
    if (!m) return kExitUsageOrIo;
    core::Diagnostics extra;
    // Pricing of the first included project; other projects must agree (warning otherwise).
    std::optional<production::PricingSettings> settings;
    std::string firstKey;
    for (std::size_t i = 0; i < m->model.projects.size() && i < m->loaded.projects.size(); ++i) {
        const auto& bp = m->loaded.projects[i];
        if (!m->model.projects[i].included || !bp.loaded) continue;
        production::PricingSettings s = bp.loaded->project.settings.pricing.value_or(production::PricingSettings{});
        s.currency = bp.loaded->project.metadata.currency;
        if (!settings) {
            settings = s;
            firstKey = bp.key;
        } else if (!(s == *settings)) {
            extra.push_back({core::Severity::Warning, "cli.quote.pricing_mismatch",
                             "Project '" + bp.key + "' has other pricing settings or currency (" + s.currency +
                                 ") than project '" + firstKey + "' (" + settings->currency +
                                 "); the batch quote uses those of '" + firstKey + "'.",
                             bp.key});
        }
    }
    production::QuoteOptions options;
    options.edgeBanding = m->loaded.batch.edgeBanding;
    if (a.has("sheets")) {
        const auto plans = project::batchCuttingPlans(m->model, m->loaded.batch);
        options.sheetUsage = project::quoteSheetUsage(plans, extra);
        for (const auto& d : plans.diagnostics)
            if (d.severity != core::Severity::Info) extra.push_back(d);
    }
    auto q = production::quote(m->model.model, settings.value_or(production::PricingSettings{}), options);
    if (!q) {
        io.err << "fos batch quote: " << q.error().message << "\n";
        return kExitUsageOrIo;
    }
    bool incomplete = false;
    const std::string header = batchHeader(*m, incomplete) + "\n";
    const int rc = printQuote(io, q.value(), *format, extra, header);
    return incomplete ? kExitValidationErrors : rc;
}

} // namespace

int cmdBatch(const std::vector<std::string>& args, Io& io) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h" || args[0] == "help") {
        (args.empty() ? io.err : io.out) << kBatchHelp;
        return args.empty() ? kExitUsageOrIo : kExitOk;
    }
    const std::string sub = args[0];
    const std::vector<std::string> rest(args.begin() + 1, args.end());
    for (const auto& r : rest)
        if (r == "--help" || r == "-h") {
            io.out << kBatchHelp;
            return kExitOk;
        }
    if (sub == "new") return batchNew(rest, io);
    if (sub == "add") return batchAdd(rest, io);
    if (sub == "report") return batchReport(rest, io);
    if (sub == "cut") return batchCut(rest, io);
    if (sub == "labels") return batchLabels(rest, io);
    if (sub == "quote") return batchQuote(rest, io);
    return usageError(io, "batch", "unknown subcommand '" + sub + "' (new, add, report, cut, labels, quote).");
}

} // namespace fos::cli::detail
