// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal helpers shared by the command implementations (not installed, not public API).
#include "fos/cli/cli.hpp"
#include "fos/core/diagnostic.hpp"
#include "fos/core/result.hpp"
#include "fos/production/common.hpp"
#include "fos/production/quote.hpp"
#include "fos/project/file.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fos::project {
struct CuttingPlans;
struct NestingSettings;
} // namespace fos::project

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

/// Path of a UTF-8 command-line argument. Goes through char8_t, which every standard library
/// reads as UTF-8; a plain std::string is read in the ANSI code page by MSVC on Windows, so
/// "кухня.fosp" would become "?????.fosp" there.
std::filesystem::path pathFromUtf8(std::string_view utf8);

/// UTF-8 text of `path` for messages (path::string() is not UTF-8 with every library).
std::string pathToUtf8(const std::filesystem::path& path);

/// Loads and resolves a project; prints the error and returns nullopt on failure.
std::optional<project::LoadedProject> loadProjectOrReport(Io& io, std::string_view command, const std::string& path);

/// Empty when `loaded.resolved.diagnostics` has no errors. Otherwise a multi-line notice for
/// output command `command`: "INCOMPLETE OUTPUT", the ids of the items that could not be
/// generated (and are therefore missing from every output) or a statement that all items were
/// generated but the model has errors, followed by every error diagnostic. Output commands print
/// it to stderr, still write the output of the good items, and exit kExitValidationErrors.
std::string resolveErrorNotice(std::string_view command, const project::LoadedProject& loaded);

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

/// Per-material cutting / nesting report text of `plans` followed by their diagnostics (shared by
/// `fos cut` and `fos batch cut`). Appends the sheet SVGs to write (paths in `dir`) to `files`
/// and counts the nesting SVGs among them in `nestFiles`.
std::string cuttingPlansText(const project::CuttingPlans& plans, const project::NestingSettings& nestingSettings,
                             const std::filesystem::path& dir,
                             std::vector<std::pair<std::filesystem::path, std::string>>& files, std::size_t& nestFiles);

/// Sheet usage per material from cutting plans (shared by `fos quote --sheets` and `fos batch quote
/// --sheets`): full sheets of the guillotine plans (listed remnants excluded) + nesting sheets, and
/// the guillotine cut length. A material whose plan failed is left out (priced by area) with a
/// cli.quote.plan_failed warning appended to `diagnostics`.
std::map<domain::MaterialId, production::SheetUsage> sheetUsageFromPlans(const project::CuttingPlans& plans,
                                                                         core::Diagnostics& diagnostics);
/// "text" (default), "csv" or "json" from --format; nullopt for anything else.
std::optional<std::string> quoteFormat(const ParsedArgs& args);
/// JSON document of a quote (money totals as 2-decimal strings).
std::string quoteJson(const production::Quote& quote);
/// Prints the quote in `format` (text to out with diagnostics; csv/json to out, `header` and
/// diagnostics to err) followed by `extra` + quote diagnostics. Returns kExitOk, or
/// kExitValidationErrors when the quote is incomplete or a diagnostic is an error.
int printQuote(Io& io, const production::Quote& quote, const std::string& format, const core::Diagnostics& extra,
               const std::string& header);

// Command entry points (args exclude the command name).
int cmdNew(const std::vector<std::string>& args, Io& io);
int cmdComponent(const std::vector<std::string>& args, Io& io);
int cmdInfo(const std::vector<std::string>& args, Io& io);
int cmdSet(const std::vector<std::string>& args, Io& io);
int cmdValidate(const std::vector<std::string>& args, Io& io);
int cmdReport(const std::vector<std::string>& args, Io& io);
int cmdLabels(const std::vector<std::string>& args, Io& io);
int cmdCut(const std::vector<std::string>& args, Io& io);
int cmdDrawing(const std::vector<std::string>& args, Io& io);
int cmdCam(const std::vector<std::string>& args, Io& io);
int cmdExportObj(const std::vector<std::string>& args, Io& io);
int cmdImportDxf(const std::vector<std::string>& args, Io& io);
int cmdExportDxf(const std::vector<std::string>& args, Io& io);
int cmdLinear(const std::vector<std::string>& args, Io& io);
int cmdEdgebandRolls(const std::vector<std::string>& args, Io& io);
int cmdBatch(const std::vector<std::string>& args, Io& io);
int cmdQuote(const std::vector<std::string>& args, Io& io);

} // namespace fos::cli::detail
