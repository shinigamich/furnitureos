// SPDX-License-Identifier: AGPL-3.0-only
// fos packing  (packing list + package labels, fos/production/packing.hpp)
// fos assembly (assembly sheets, fos/production/assembly_sheet.hpp)
#include "common.hpp"

#include "fos/core/units.hpp"
#include "fos/formats/json.hpp"
#include "fos/production/assembly_sheet.hpp"
#include "fos/production/csv.hpp"
#include "fos/production/packing.hpp"

namespace fos::cli::detail {
namespace {

const char* kPackingHelp =
    "Usage: fos packing <file.fosp> [--max-kg 25] [--max-length mm] [--max-width mm] [--max-height mm]\n"
    "                   [--padding 10] [--format text|csv|json] [--svg dir]\n\n"
    "Packing list for delivery / flat-pack: per item, the panels grouped into packages (simple first-fit\n"
    "heuristic, largest panels at the bottom) within the weight limit (--max-kg, default 25) and the optional\n"
    "package size limits (outer size incl. padding; 0 = no limit), plus one hardware bag per item (fittings\n"
    "and fasteners by definition and count). Package size = largest panel + 2 x padding, height = stacked\n"
    "thickness + 2 x padding (mm). Weights come ONLY from the material density (kg/m3, library field\n"
    "'density'); without it a panel and its package are 'weight unknown' (never estimated). Weights are\n"
    "panels only: packaging and hardware are not included.\n"
    "--svg writes package labels (one per package and per hardware bag) as package-labels-<n>.svg.\n"
    "Exit code 1 when the project has model errors.\n";

const char* kAssemblyHelp =
    "Usage: fos assembly <file.fosp> [--svg dir]\n\n"
    "Assembly sheet per item: numbered steps with the parts and hardware counts of each step.\n"
    "The order is GENERIC, derived only from part roles and hardware kinds (carcass sides/bottom/top ->\n"
    "back -> shelves/dividers -> drawer boxes -> fronts -> handles -> other); it is not a verified\n"
    "assembly instruction for the design. --svg writes assembly-<item id>.svg per item (A4 width, text only).\n"
    "Exit code 1 when the project has model errors.\n";

core::Diagnostics withoutInfo(const core::Diagnostics& ds) {
    core::Diagnostics out;
    for (const auto& d : ds)
        if (d.severity != core::Severity::Info) out.push_back(d);
    return out;
}

formats::Json weightJson(const std::optional<double>& kg) {
    return kg ? formats::Json(core::roundTo(*kg, 3)) : formats::Json(nullptr);
}

std::string packingJson(const production::PackingList& list) {
    using formats::Json;
    Json j = Json::object();
    j["maxPackageWeightKg"] = list.options.maxPackageWeightKg;
    j["maxPackageLength"] = list.options.maxPackageLength;
    j["maxPackageWidth"] = list.options.maxPackageWidth;
    j["maxPackageHeight"] = list.options.maxPackageHeight;
    j["padding"] = list.options.padding;
    Json assemblies = Json::array();
    for (const auto& a : list.assemblies) {
        Json ja = Json::object();
        ja["id"] = a.assemblyId.str();
        ja["name"] = a.assemblyName;
        Json packages = Json::array();
        for (const auto& p : a.packages) {
            Json jp = Json::object();
            jp["id"] = p.id;
            jp["number"] = p.number;
            jp["length"] = core::roundTo(p.length, 3);
            jp["width"] = core::roundTo(p.width, 3);
            jp["height"] = core::roundTo(p.height, 3);
            jp["weightKg"] = weightJson(p.weightKg);
            jp["overweight"] = p.overweight;
            jp["oversize"] = p.oversize;
            Json parts = Json::array();
            for (const auto& part : p.parts)
                parts.push_back({{"number", part.partNumber}, {"id", part.partId.str()}, {"name", part.name},
                                 {"material", part.materialName}, {"length", part.length}, {"width", part.width},
                                 {"thickness", part.thickness}, {"weightKg", weightJson(part.weightKg)}});
            jp["parts"] = std::move(parts);
            packages.push_back(std::move(jp));
        }
        ja["packages"] = std::move(packages);
        Json bag = Json::object();
        bag["id"] = a.bag.id;
        for (const auto& [key, lines] : {std::pair{"fittings", &a.bag.fittings}, std::pair{"fasteners", &a.bag.fasteners}}) {
            Json arr = Json::array();
            for (const auto& l : *lines)
                arr.push_back({{"definition", l.definition.str()}, {"name", l.name}, {"article", l.article},
                               {"kind", domain::toString(l.kind)}, {"unknownDefinition", l.unknownDefinition},
                               {"quantity", l.quantity}});
            bag[key] = std::move(arr);
        }
        ja["hardwareBag"] = std::move(bag);
        assemblies.push_back(std::move(ja));
    }
    j["assemblies"] = std::move(assemblies);
    Json diags = Json::array();
    for (const auto& d : list.diagnostics)
        diags.push_back({{"severity", severityText(d.severity)}, {"code", d.code}, {"entityId", d.entityId},
                         {"message", d.message}});
    j["diagnostics"] = std::move(diags);
    return j.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

/// Reads a numeric option into `target`; false (after printing a usage error) when invalid.
bool numberOption(Io& io, const ParsedArgs& a, const char* name, double& target, int& rc) {
    const auto v = a.value(name);
    if (!v) return true;
    const auto n = parseNumber(*v);
    if (!n) {
        rc = usageError(io, "packing", std::string("--") + name + " expects a number, got '" + *v + "'.");
        return false;
    }
    target = *n;
    return true;
}

} // namespace

int cmdPacking(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"max-kg"}, {"max-length"}, {"max-width"}, {"max-height"}, {"padding"},
                                   {"format"}, {"svg"}});
    if (!parsed) return usageError(io, "packing", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kPackingHelp;
        return kExitOk;
    }
    if (a.positional.size() != 1) return usageError(io, "packing", "expected exactly one project file.");
    const std::string format = a.value("format").value_or("text");
    if (format != "text" && format != "csv" && format != "json")
        return usageError(io, "packing", "--format must be text, csv or json.");
    production::PackingOptions options;
    int rc = kExitOk;
    if (!numberOption(io, a, "max-kg", options.maxPackageWeightKg, rc) ||
        !numberOption(io, a, "max-length", options.maxPackageLength, rc) ||
        !numberOption(io, a, "max-width", options.maxPackageWidth, rc) ||
        !numberOption(io, a, "max-height", options.maxPackageHeight, rc) ||
        !numberOption(io, a, "padding", options.padding, rc))
        return rc;
    auto loaded = loadProjectOrReport(io, "packing", a.positional[0]);
    if (!loaded) return kExitUsageOrIo;
    auto list = production::packingList(loaded->resolved.model, options);
    if (!list) return usageError(io, "packing", list.error().message);
    const auto& pl = list.value();

    if (format == "json") {
        io.out << packingJson(pl);
    } else if (format == "csv") {
        io.out << production::toCsv(production::packingTable(pl));
        if (!pl.diagnostics.empty()) printDiagnostics(io.err, pl.diagnostics);
    } else {
        io.out << production::packingListText(pl);
        if (!pl.diagnostics.empty()) {
            io.out << "\nDiagnostics:\n";
            printDiagnostics(io.out, pl.diagnostics);
        }
    }
    if (const auto dir = a.value("svg")) {
        auto pages = production::renderPackageLabelsSvg(pl, loaded->project.metadata.name);
        if (!pages) {
            io.err << "fos packing: " << pages.error().message << "\n";
            return kExitUsageOrIo;
        }
        const std::filesystem::path path = pathFromUtf8(*dir);
        if (!ensureOutputDirectory(io, "packing", path)) return kExitUsageOrIo;
        for (std::size_t i = 0; i < pages.value().size(); ++i)
            if (!writeOutputFile(io, "packing", path / ("package-labels-" + std::to_string(i + 1) + ".svg"),
                                 pages.value()[i]))
                return kExitUsageOrIo;
        io.err << "Wrote " << pages.value().size() << " package label page(s) to " << pathToUtf8(path) << "\n";
    }
    const std::string incomplete = resolveErrorNotice("packing", *loaded);
    io.err << incomplete;
    return core::hasErrors(pl.diagnostics) || !incomplete.empty() ? kExitValidationErrors : kExitOk;
}

int cmdAssembly(const std::vector<std::string>& args, Io& io) {
    auto parsed = parseArgs(args, {{"svg"}});
    if (!parsed) return usageError(io, "assembly", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kAssemblyHelp;
        return kExitOk;
    }
    if (a.positional.size() != 1) return usageError(io, "assembly", "expected exactly one project file.");
    auto loaded = loadProjectOrReport(io, "assembly", a.positional[0]);
    if (!loaded) return kExitUsageOrIo;
    const auto sheets = production::assemblySheets(loaded->resolved.model);
    for (std::size_t i = 0; i < sheets.sheets.size(); ++i)
        io.out << (i ? "\n" : "") << production::assemblySheetText(sheets.sheets[i]);
    if (sheets.sheets.empty()) io.out << "No items with parts or hardware.\n";
    const auto diags = withoutInfo(sheets.diagnostics);
    if (!diags.empty()) {
        io.out << "\nDiagnostics:\n";
        printDiagnostics(io.out, diags);
    }
    if (const auto dir = a.value("svg")) {
        const std::filesystem::path path = pathFromUtf8(*dir);
        if (!ensureOutputDirectory(io, "assembly", path)) return kExitUsageOrIo;
        for (const auto& s : sheets.sheets)
            if (!writeOutputFile(io, "assembly", path / ("assembly-" + safeFileName(s.assemblyId.str()) + ".svg"),
                                 production::assemblySheetSvg(s, loaded->project.metadata.name)))
                return kExitUsageOrIo;
        io.err << "Wrote " << sheets.sheets.size() << " assembly sheet(s) to " << pathToUtf8(path) << "\n";
    }
    const std::string incomplete = resolveErrorNotice("assembly", *loaded);
    io.err << incomplete;
    return core::hasErrors(sheets.diagnostics) || !incomplete.empty() ? kExitValidationErrors : kExitOk;
}

} // namespace fos::cli::detail
