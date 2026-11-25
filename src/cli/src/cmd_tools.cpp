// SPDX-License-Identifier: AGPL-3.0-only
// fos tools validate|list|export-default (.fostools CAM tool tables)
#include "cmd_tools.hpp"

#include "fos/cli/demo_cam.hpp"
#include "fos/core/units.hpp"
#include "fos/formats/tool_table.hpp"

namespace fos::cli::detail {
namespace {

const char* kToolsHelp =
    "Usage: fos tools validate <file.fostools>\n"
    "       fos tools list <file.fostools>\n"
    "       fos tools export-default -o <file.fostools> [--force]\n\n"
    "CAM tool tables (.fostools, container kind 'furnitureos-tools', version 1). Each tool has an\n"
    "id, a tool number (Tn), a name, a type (drill | flat_end_mill), diameter and flute length in\n"
    "mm, and optionally overall length, max depth per pass (required for flat end mills), spindle\n"
    "rpm, feed and plunge rate (mm/min) and notes. `fos cam --tools FILE` uses such a table instead\n"
    "of the built-in demo tool library; CAM then needs rpm, feed and plunge rate for every tool.\n"
    "  validate        Lists every finding with its JSON path (exit 1 when there are errors).\n"
    "  list            Prints the tools as a table (exit 1 when the table has errors).\n"
    "  export-default  Writes the built-in FICTIONAL demo tool library used by `fos cam` (not real\n"
    "                  tools) as a starting point. An existing file is replaced only with --force.\n"
    "Tool data is user data: FurnitureOS does not check it against real tools or any machine.\n"
    "Exit codes: 0 ok, 1 invalid tool table, 2 usage or I/O error.\n";

/// Reads and decodes the file; prints errors. nullopt with `exitCode` set on failure.
std::optional<formats::LoadedToolTable> loadOrReport(Io& io, std::string_view command, const std::string& path,
                                                     int& exitCode) {
    auto loaded = formats::loadToolTable(pathFromUtf8(path));
    if (!loaded) {
        const auto code = loaded.error().code;
        const bool io_error = code == core::ErrorCode::IoError || code == core::ErrorCode::ConstraintViolation;
        io.err << "fos " << command << ": " << (io_error ? "cannot read '" + path + "': " : std::string())
               << loaded.error().message << "\n";
        exitCode = io_error ? kExitUsageOrIo : kExitValidationErrors;
        return std::nullopt;
    }
    return std::move(loaded.value());
}

std::string optionalNumber(const std::optional<double>& v) { return v ? core::formatLength(*v) : "-"; }

int toolsValidate(const ParsedArgs& a, Io& io) {
    if (a.positional.size() != 2) return usageError(io, "tools", "expected: validate <file.fostools>.");
    int exitCode = kExitOk;
    auto loaded = loadOrReport(io, "tools validate", a.positional[1], exitCode);
    if (!loaded) return exitCode;
    const auto& ds = loaded->diagnostics;
    printDiagnostics(io.out, ds);
    io.out << a.positional[1] << ": " << loaded->table.tools.size() << " tool(s), " << errorCount(ds) << " error(s), "
           << warningCount(ds) << " warning(s)\n";
    return core::hasErrors(ds) ? kExitValidationErrors : kExitOk;
}

int toolsList(const ParsedArgs& a, Io& io) {
    if (a.positional.size() != 2) return usageError(io, "tools", "expected: list <file.fostools>.");
    int exitCode = kExitOk;
    auto loaded = loadOrReport(io, "tools list", a.positional[1], exitCode);
    if (!loaded) return exitCode;
    production::Table table;
    table.header = {"T", "Id", "Type", "Diameter", "Flute", "Overall", "Depth/pass", "RPM", "Feed", "Plunge", "Name"};
    for (const auto& t : loaded->table.tools)
        table.rows.push_back({"T" + std::to_string(t.number), t.id, formats::toolTypeToken(t.type),
                              core::formatLength(t.diameter), core::formatLength(t.fluteLength),
                              optionalNumber(t.overallLength), optionalNumber(t.maxDepthPerPass),
                              optionalNumber(t.spindleRpm), optionalNumber(t.feedRate), optionalNumber(t.plungeRate),
                              t.name});
    io.out << textTable(table) << "Lengths in mm, feeds in mm/min.\n";
    if (!loaded->diagnostics.empty()) {
        io.err << "fos tools list: findings:\n";
        printDiagnostics(io.err, loaded->diagnostics);
    }
    return core::hasErrors(loaded->diagnostics) ? kExitValidationErrors : kExitOk;
}

int toolsExportDefault(const ParsedArgs& a, Io& io) {
    if (a.positional.size() != 1) return usageError(io, "tools", "export-default takes no positional arguments.");
    const auto out = a.value("out");
    if (!out) return usageError(io, "tools", "export-default needs -o <file.fostools>.");
    const auto path = pathFromUtf8(*out);
    std::error_code ec;
    if (!a.has("force") && std::filesystem::exists(path, ec))
        return usageError(io, "tools", "'" + *out + "' exists; use --force to replace it.");
    const auto parent = path.parent_path();
    if (!parent.empty() && !std::filesystem::is_directory(parent, ec)) {
        io.err << "fos tools: cannot write '" << *out << "': directory '" << pathToUtf8(parent)
               << "' does not exist.\n";
        return kExitUsageOrIo;
    }
    formats::ToolTable table = formats::toolTableFromLibrary(demoToolLibrary());
    table.extra["note"] = "FICTIONAL demo tool library of FurnitureOS: not real tools, not verified for any machine.";
    formats::SaveOptions options;
    options.backupCount = 0;
    auto saved = formats::saveToolTable(path, table, formats::AppInfo{"FurnitureOS", FOS_CLI_VERSION}, options);
    if (!saved) {
        io.err << "fos tools: cannot write '" << *out << "': " << saved.error().message << "\n";
        return kExitUsageOrIo;
    }
    io.out << "Wrote " << *out << " (" << table.tools.size()
           << " tools of the FICTIONAL demo library; edit them to describe your own tools)\n";
    return kExitOk;
}

} // namespace

std::optional<cam::ToolLibrary> loadToolLibraryOrReport(Io& io, std::string_view command, const std::string& path,
                                                        int& exitCode) {
    auto loaded = loadOrReport(io, command, path, exitCode);
    if (!loaded) return std::nullopt;
    if (core::hasErrors(loaded->diagnostics)) {
        io.err << "fos " << command << ": '" << path << "' is not a valid tool table:\n";
        core::Diagnostics errors;
        for (const auto& d : loaded->diagnostics)
            if (d.severity == core::Severity::Error) errors.push_back(d);
        printDiagnostics(io.err, errors);
        exitCode = kExitValidationErrors;
        return std::nullopt;
    }
    printDiagnostics(io.err, loaded->diagnostics); // warnings only
    auto library = formats::toToolLibrary(loaded->table);
    if (!library) {
        io.err << "fos " << command << ": '" << path << "' cannot be used for CAM: " << library.error().message << "\n";
        exitCode = kExitValidationErrors;
        return std::nullopt;
    }
    return std::move(library.value());
}

int cmdTools(const std::vector<std::string>& args, Io& io) {
    // `-o FILE` is accepted as a short form of `--out FILE`.
    std::vector<std::string> normalized;
    for (const auto& arg : args) normalized.push_back(arg == "-o" ? std::string("--out") : arg);
    auto parsed = parseArgs(normalized, {{"out"}, {"force", false}});
    if (!parsed) return usageError(io, "tools", parsed.error().message);
    const ParsedArgs& a = parsed.value();
    if (a.help) {
        io.out << kToolsHelp;
        return kExitOk;
    }
    if (a.positional.empty()) return usageError(io, "tools", "expected a subcommand: validate, list or export-default.");
    const std::string& sub = a.positional[0];
    if (sub != "export-default" && (a.has("out") || a.has("force")))
        return usageError(io, "tools", "-o/--out and --force apply to export-default only.");
    if (sub == "validate") return toolsValidate(a, io);
    if (sub == "list") return toolsList(a, io);
    if (sub == "export-default") return toolsExportDefault(a, io);
    return usageError(io, "tools", "unknown subcommand '" + sub + "' (expected validate, list or export-default).");
}

} // namespace fos::cli::detail
