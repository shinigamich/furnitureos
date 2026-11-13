// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal helpers shared by the command implementations (not installed, not public API).
#include "fos/cli/cli.hpp"
#include "fos/core/diagnostic.hpp"
#include "fos/core/result.hpp"
#include "fos/production/common.hpp"
#include "fos/project/file.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fos::cli::detail {

struct Io {
    std::ostream& out;
    std::ostream& err;
};

/// An option a command accepts. `takesValue` options are given as `--name value` or
/// `--name=value`; flags as `--name`.
struct OptionSpec {
    std::string name; ///< Without the leading dashes.
    bool takesValue = true;
};

struct ParsedArgs {
    std::vector<std::string> positional;
    std::map<std::string, std::string> values; ///< Options with a value (last one wins).
    std::set<std::string> flags;
    bool help = false; ///< --help or -h was given.

    bool has(const std::string& name) const { return values.count(name) != 0 || flags.count(name) != 0; }
    std::optional<std::string> value(const std::string& name) const;
};

/// Parses `args` (the arguments after the command name). Unknown options and missing option
/// values are InvalidArgument errors naming the option. "--" ends option parsing.
core::Result<ParsedArgs> parseArgs(const std::vector<std::string>& args, const std::vector<OptionSpec>& specs);

/// Prints "fos <command>: <message>" plus a hint to `--help` and returns kExitUsageOrIo.
int usageError(Io& io, std::string_view command, std::string_view message);

/// Loads and resolves a project; prints the error and returns nullopt on failure.
std::optional<project::LoadedProject> loadProjectOrReport(Io& io, std::string_view command, const std::string& path);

/// Atomically writes a text file (no backups); prints the error on failure.
bool writeOutputFile(Io& io, std::string_view command, const std::filesystem::path& path, std::string_view content);

/// Strict number parsing ("900", "-3.5", "1e3"); nullopt for anything else (incl. trailing text).
std::optional<double> parseNumber(std::string_view text);

/// "error" / "warning" / "info".
const char* severityText(core::Severity severity) noexcept;

/// One line per diagnostic: "<indent>[severity] code (entity): message".
void printDiagnostics(std::ostream& out, const core::Diagnostics& diagnostics, std::string_view indent = "  ");

/// Count of Error diagnostics.
std::size_t errorCount(const core::Diagnostics& diagnostics) noexcept;
std::size_t warningCount(const core::Diagnostics& diagnostics) noexcept;

/// Plain text rendering of a table: columns padded to the widest cell (UTF-8 aware), header,
/// a dashed rule, rows. No trailing spaces.
std::string textTable(const production::Table& table);

/// Creates `dir` (and parents) if needed; prints the error on failure.
bool ensureOutputDirectory(Io& io, std::string_view command, const std::filesystem::path& dir);

/// A file-name-safe version of an id: every character outside [A-Za-z0-9._-] becomes '_'.
std::string safeFileName(std::string_view id);

// Command entry points (args exclude the command name).
int cmdNew(const std::vector<std::string>& args, Io& io);
int cmdInfo(const std::vector<std::string>& args, Io& io);
int cmdSet(const std::vector<std::string>& args, Io& io);
int cmdValidate(const std::vector<std::string>& args, Io& io);
int cmdReport(const std::vector<std::string>& args, Io& io);
int cmdLabels(const std::vector<std::string>& args, Io& io);
int cmdCut(const std::vector<std::string>& args, Io& io);
int cmdDrawing(const std::vector<std::string>& args, Io& io);
int cmdCam(const std::vector<std::string>& args, Io& io);
int cmdExportObj(const std::vector<std::string>& args, Io& io);

} // namespace fos::cli::detail
