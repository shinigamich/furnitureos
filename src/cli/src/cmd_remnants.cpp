// SPDX-License-Identifier: AGPL-3.0-only
// fos remnants  (workshop remnant stock, .fosremnants; fos/project/remnant_store.hpp, remnant_use.hpp)
#include "common.hpp"

#include "fos/core/units.hpp"
#include "fos/project/remnant_use.hpp"

#include <chrono>
#include <cstdio>
#include <sstream>

namespace fos::cli::detail {
namespace {

const char* kRemnantsHelp =
    "Usage: fos remnants <subcommand> <store.fosremnants> ...\n\n"
    "Workshop remnant (offcut) stock, kept in one .fosremnants file independent of any project.\n"
    "Every entry is ONE physical board: id, material id, length x width x thickness (mm), grain\n"
    "(none | length | width: the side the grain runs along), location, note, date added, reserved-by job.\n\n"
    "  list <store> [--material M]\n"
    "      Print the entries (optionally of one material).\n"
    "  add <store> --material M --length L --width W --thickness T [--grain none|length|width]\n"
    "      [--location TEXT] [--note TEXT] [--reserved-by JOB] [--date YYYY-MM-DD] [--id ID]\n"
    "      Add one board (the store file is created if it does not exist; id default R0001, R0002, ...).\n"
    "  remove <store> <id> [<id> ...]\n"
    "      Remove boards (all ids must exist, otherwise nothing is removed).\n"
    "  import-offcuts <store> <file.fosp> [--min-length 300] [--min-width 100] [--location TEXT]\n"
    "      [--date YYYY-MM-DD] [--force]\n"
    "      Recompute the project's cutting plans exactly as 'fos cut' does (same settings, deterministic)\n"
    "      and add every offcut whose longer side >= min length and shorter side >= min width. Refused\n"
    "      when offcuts of the same project are already in the store (use --force to add them again),\n"
    "      when the project has model errors or a plan failed.\n"
    "  use <store> <file.fosp> [--commit] [--prefer-remnants] [--job NAME] [--out-dir DIR]\n"
    "      [--min-length 300] [--min-width 100] [--location TEXT] [--date YYYY-MM-DD]\n"
    "      Cut the project with the store's free boards of the same material id and thickness offered\n"
    "      first (as listed remnants, before full sheets) and print which boards were used.\n"
    "      --prefer-remnants uses the PreferRemnants objective for those materials (otherwise the\n"
    "      project objective decides; MinimizeSheets may skip a board that would cost an extra sheet).\n"
    "      --job NAME also allows boards reserved by NAME (default: the project name).\n"
    "      --out-dir writes the sheet SVGs and cutting-summary.txt as 'fos cut' does.\n"
    "      --commit removes the used boards from the store and adds the new offcuts (refused when a\n"
    "      plan has errors or the project has model errors). Without --commit the store is not changed.\n\n"
    "Every change is printed. The store is written atomically with 3 rotating backups (.bak1..3).\n"
    "Only guillotine (sawn) plans are considered; shaped parts nested for routing are not.\n";

constexpr const char* kCmd = "remnants";

std::string todayUtc() {
    const auto days = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
    const std::chrono::year_month_day ymd{days};
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u", static_cast<int>(ymd.year()), static_cast<unsigned>(ymd.month()),
                  static_cast<unsigned>(ymd.day()));
    return buf;
}

std::optional<project::RemnantStore> loadStore(Io& io, const std::string& file, bool createIfMissing) {
    const auto path = pathFromUtf8(file);
    std::error_code ec;
    if (createIfMissing && !std::filesystem::exists(path, ec)) {
        io.out << "Store " << file << " does not exist yet; it will be created.\n";
        return project::RemnantStore{};
    }
    auto loaded = project::loadRemnantStore(path);
    if (!loaded) {
        io.err << "fos remnants: cannot read store '" << file << "': " << loaded.error().message << "\n";
        return std::nullopt;
    }
    return std::move(loaded).value();
}

bool saveStore(Io& io, const std::string& file, const project::RemnantStore& store) {
    auto saved = project::saveRemnantStore(pathFromUtf8(file), store);
    if (!saved) {
        io.err << "fos remnants: the store was NOT changed: " << saved.error().message << "\n";
        return false;
    }
    io.out << "Saved " << file << " (" << store.remnants.size() << " remnant(s)).\n";
    return true;
}

/// --date or today; nullopt (after a usage error) when invalid.
std::optional<std::string> dateOption(const ParsedArgs& a, Io& io, int& code) {
    const std::string date = a.value("date").value_or(todayUtc());
    if (!project::isIsoDate(date)) {
        code = usageError(io, kCmd, "--date must be YYYY-MM-DD, got '" + date + "'.");
        return std::nullopt;
    }
    return date;
}

/// Reads a positive (or non-negative) number option; false after a usage error.
bool numberOption(const ParsedArgs& a, const std::string& name, double& out, bool allowZero, Io& io, int& code) {
    const auto text = a.value(name);
    if (!text) return true;
    const auto v = parseNumber(*text);
    if (!v || !(allowZero ? *v >= 0 : *v > 0)) {
        code = usageError(io, kCmd, "--" + name + " must be a " + (allowZero ? "non-negative" : "positive") +
                                        " number of millimetres, got '" + *text + "'.");
        return false;
    }
    out = *v;
    return true;
}

std::string offcutSource(const project::LoadedProject& loaded, const std::string& file) {
    return "offcuts of '" + loaded.project.metadata.name + "' (" + pathToUtf8(pathFromUtf8(file).filename()) + ")";
}

int cmdList(const ParsedArgs& a, Io& io) {
    if (a.positional.size() != 2) return usageError(io, kCmd, "list expects exactly one store file.");
    auto store = loadStore(io, a.positional[1], false);
    if (!store) return kExitUsageOrIo;
    const auto material = a.value("material");
    production::Table t;
    t.header = {"Id", "Material", "Length", "Width", "Thick", "Grain", "Added", "Location", "Reserved by", "Note"};
    double area = 0;
    for (const auto& r : store->remnants) {
        if (material && r.materialId != *material) continue;
        t.rows.push_back({r.id, r.materialId, core::formatLength(r.length), core::formatLength(r.width),
                          core::formatLength(r.thickness), project::remnantGrainToken(r.grain), r.dateAdded, r.location,
                          r.reservedBy, r.note});
        area += r.length * r.width;
    }
    io.out << textTable(t) << t.rows.size() << " remnant(s), " << core::formatLength(area / 1e6, 3)
           << " m2. Lengths in mm.\n";
    return kExitOk;
}

int cmdAdd(const ParsedArgs& a, Io& io) {
    if (a.positional.size() != 2) return usageError(io, kCmd, "add expects exactly one store file.");
    int code = kExitOk;
    project::StoredRemnant r;
    r.materialId = a.value("material").value_or("");
    if (r.materialId.empty()) return usageError(io, kCmd, "add needs --material.");
    for (const char* name : {"length", "width", "thickness"})
        if (!a.value(name)) return usageError(io, kCmd, std::string("add needs --") + name + ".");
    if (!numberOption(a, "length", r.length, false, io, code) || !numberOption(a, "width", r.width, false, io, code) ||
        !numberOption(a, "thickness", r.thickness, false, io, code))
        return code;
    if (const auto g = a.value("grain")) {
        const auto grain = project::remnantGrainFromToken(*g);
        if (!grain) return usageError(io, kCmd, "--grain must be none, length or width, got '" + *g + "'.");
        r.grain = *grain;
    }
    const auto date = dateOption(a, io, code);
    if (!date) return code;
    r.dateAdded = *date;
    r.location = a.value("location").value_or("");
    r.note = a.value("note").value_or("");
    r.reservedBy = a.value("reserved-by").value_or("");
    auto store = loadStore(io, a.positional[1], true);
    if (!store) return kExitUsageOrIo;
    r.id = a.value("id").value_or(project::nextRemnantId(*store));
    if (store->find(r.id) != nullptr)
        return usageError(io, kCmd, "remnant id '" + r.id + "' is already in the store.");
    store->remnants.push_back(r);
    if (!saveStore(io, a.positional[1], *store)) return kExitUsageOrIo;
    io.out << "Added " << project::describeRemnant(r) << "\n";
    return kExitOk;
}

int cmdRemove(const ParsedArgs& a, Io& io) {
    if (a.positional.size() < 3) return usageError(io, kCmd, "remove expects a store file and at least one id.");
    auto store = loadStore(io, a.positional[1], false);
    if (!store) return kExitUsageOrIo;
    std::set<std::string> ids(a.positional.begin() + 2, a.positional.end());
    for (const auto& id : ids)
        if (store->find(id) == nullptr) {
            io.err << "fos remnants: remnant '" << id << "' is not in the store; nothing was removed.\n";
            return kExitValidationErrors;
        }
    project::RemnantStore next;
    next.extra = store->extra;
    std::vector<project::StoredRemnant> removed;
    for (const auto& r : store->remnants)
        (ids.count(r.id) ? removed : next.remnants).push_back(r);
    if (!saveStore(io, a.positional[1], next)) return kExitUsageOrIo;
    for (const auto& r : removed) io.out << "Removed " << project::describeRemnant(r) << "\n";
    return kExitOk;
}

bool offcutOptions(const ParsedArgs& a, Io& io, int& code, project::OffcutImportOptions& o) {
    if (!numberOption(a, "min-length", o.minLength, true, io, code) ||
        !numberOption(a, "min-width", o.minWidth, true, io, code))
        return false;
    const auto date = dateOption(a, io, code);
    if (!date) return false;
    o.date = *date;
    o.location = a.value("location").value_or("");
    return true;
}

/// Prints the incomplete-model notice / plan errors; true when stock must not be changed.
bool plansUnsafe(Io& io, const project::LoadedProject& loaded, const project::CuttingPlans& plans) {
    const std::string notice = resolveErrorNotice(kCmd, loaded);
    io.err << notice;
    if (core::hasErrors(plans.diagnostics)) {
        io.err << "fos remnants: the cutting plans have errors:\n";
        printDiagnostics(io.err, plans.diagnostics);
    }
    return !notice.empty() || core::hasErrors(plans.diagnostics);
}

int cmdImportOffcuts(const ParsedArgs& a, Io& io) {
    if (a.positional.size() != 3) return usageError(io, kCmd, "import-offcuts expects a store file and a project file.");
    int code = kExitOk;
    project::OffcutImportOptions o;
    if (!offcutOptions(a, io, code, o)) return code;
    auto loaded = loadProjectOrReport(io, kCmd, a.positional[2]);
    if (!loaded) return kExitUsageOrIo;
    auto store = loadStore(io, a.positional[1], true);
    if (!store) return kExitUsageOrIo;
    o.source = offcutSource(*loaded, a.positional[2]);
    if (!a.has("force"))
        for (const auto& r : store->remnants)
            if (r.source == o.source) {
                io.err << "fos remnants: the store already holds " << o.source << " (e.g. " << r.id
                       << "); nothing was added. Use --force to add them again.\n";
                return kExitValidationErrors;
            }
    const auto plans = project::cuttingPlans(loaded->resolved, loaded->project.cutting);
    if (plansUnsafe(io, *loaded, plans)) {
        io.err << "fos remnants: offcuts were NOT imported.\n";
        return kExitValidationErrors;
    }
    const auto added = project::remnantsFromOffcuts(plans, loaded->resolved.model, *store, o);
    for (const auto& r : added) store->remnants.push_back(r);
    if (added.empty()) {
        io.out << "No offcut of at least " << core::formatLength(o.minLength) << " x " << core::formatLength(o.minWidth)
               << " mm in the cutting plans; the store was not changed.\n";
        return kExitOk;
    }
    if (!saveStore(io, a.positional[1], *store)) return kExitUsageOrIo;
    for (const auto& r : added) io.out << "Added " << project::describeRemnant(r) << " (" << r.note << ")\n";
    io.out << added.size() << " offcut(s) of at least " << core::formatLength(o.minLength) << " x "
           << core::formatLength(o.minWidth) << " mm added.\n";
    return kExitOk;
}

int cmdUse(const ParsedArgs& a, Io& io) {
    if (a.positional.size() != 3) return usageError(io, kCmd, "use expects a store file and a project file.");
    int code = kExitOk;
    project::OffcutImportOptions o;
    if (!offcutOptions(a, io, code, o)) return code;
    auto loaded = loadProjectOrReport(io, kCmd, a.positional[2]);
    if (!loaded) return kExitUsageOrIo;
    auto store = loadStore(io, a.positional[1], false);
    if (!store) return kExitUsageOrIo;
    const auto outDir = a.value("out-dir");
    if (outDir && !ensureOutputDirectory(io, kCmd, pathFromUtf8(*outDir))) return kExitUsageOrIo;

    project::StoreUseOptions uo;
    uo.jobName = a.value("job").value_or(loaded->project.metadata.name);
    uo.preferRemnants = a.has("prefer-remnants");
    const auto run = project::cuttingPlansWithStore(loaded->resolved, loaded->project.cutting, *store, uo);

    std::ostringstream s;
    s << "Cutting '" << loaded->project.metadata.name << "' with remnants from " << a.positional[1]
      << " offered first (HEURISTIC optimiser).\n"
      << "Offered: " << run.offered.size() << " remnant(s)";
    for (std::size_t i = 0; i < run.offered.size(); ++i) s << (i ? ", " : ": ") << run.offered[i];
    s << "\n";
    for (const auto& k : run.skipped) s << "Not offered: " << k.reason << "\n";
    if (run.used.empty()) s << "No store remnant was used.\n";
    for (const auto& u : run.used) {
        const auto* r = store->find(u.remnantId);
        s << "USED " << (r ? project::describeRemnant(*r) : u.remnantId) << " -> sheet " << u.sheetIndex + 1 << " of "
          << u.materialId << ", " << u.pieceCount << " piece(s)\n";
    }
    for (const auto& plan : run.plans.plans) {
        if (!plan.result) continue;
        int full = 0;
        for (const auto& sh : plan.result->sheets)
            if (sh.stockIndex >= 0 && !plan.job.stock[static_cast<std::size_t>(sh.stockIndex)].isRemnant) ++full;
        s << "Material " << plan.job.id << ": " << plan.result->sheets.size() << " sheet(s), " << full
          << " of them full (purchased) boards, " << plan.result->totals.unplacedCount << " piece(s) unplaced.\n";
    }
    if (outDir) {
        std::vector<std::pair<std::filesystem::path, std::string>> files;
        std::size_t nestFiles = 0;
        const auto dir = pathFromUtf8(*outDir);
        const std::string text = cuttingPlansText(run.plans, loaded->project.cutting.nestingSettings(), dir, files, nestFiles);
        for (const auto& [path, content] : files)
            if (!writeOutputFile(io, kCmd, path, content)) return kExitUsageOrIo;
        if (!writeOutputFile(io, kCmd, dir / "cutting-summary.txt", s.str() + text)) return kExitUsageOrIo;
        s << "Wrote " << files.size() << " SVG(s) and cutting-summary.txt to " << *outDir << "\n";
    }
    io.out << s.str();
    const bool unsafe = plansUnsafe(io, *loaded, run.plans);
    if (!a.has("commit")) {
        io.out << "Store not changed (dry run; add --commit to remove the used remnants and add the new offcuts).\n";
        return unsafe ? kExitValidationErrors : kExitOk;
    }
    if (unsafe) {
        io.err << "fos remnants: --commit refused; the store was NOT changed.\n";
        return kExitValidationErrors;
    }
    o.source = offcutSource(*loaded, a.positional[2]);
    auto commit = project::commitStoreUse(*store, run, loaded->resolved.model, o);
    if (!commit) {
        io.err << "fos remnants: " << commit.error().message << "\n";
        return kExitValidationErrors;
    }
    if (!saveStore(io, a.positional[1], commit.value().store)) return kExitUsageOrIo;
    for (const auto& r : commit.value().removed) io.out << "Removed (used) " << project::describeRemnant(r) << "\n";
    for (const auto& r : commit.value().added) io.out << "Added " << project::describeRemnant(r) << " (" << r.note << ")\n";
    io.out << commit.value().removed.size() << " remnant(s) removed, " << commit.value().added.size()
           << " offcut(s) added.\n";
    return kExitOk;
}

} // namespace

int cmdRemnants(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"material"}, {"length"}, {"width"}, {"thickness"}, {"grain"}, {"location"},
                                   {"note"}, {"reserved-by"}, {"date"}, {"id"}, {"min-length"}, {"min-width"},
                                   {"job"}, {"out-dir"}, {"force", false}, {"commit", false},
                                   {"prefer-remnants", false}});
    if (!parsed) return usageError(io, kCmd, parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help || a.positional.empty()) {
        if (!a.help) return usageError(io, kCmd, "expected a subcommand (list, add, remove, import-offcuts, use).");
        io.out << kRemnantsHelp;
        return kExitOk;
    }
    const std::string& sub = a.positional[0];
    if (sub == "list") return cmdList(a, io);
    if (sub == "add") return cmdAdd(a, io);
    if (sub == "remove") return cmdRemove(a, io);
    if (sub == "import-offcuts") return cmdImportOffcuts(a, io);
    if (sub == "use") return cmdUse(a, io);
    return usageError(io, kCmd, "unknown subcommand '" + sub + "' (list, add, remove, import-offcuts, use).");
}

} // namespace fos::cli::detail
