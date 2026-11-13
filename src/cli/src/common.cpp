// SPDX-License-Identifier: AGPL-3.0-only
#include "common.hpp"

#include "fos/formats/atomic_file.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>

namespace fos::cli::detail {

std::optional<std::string> ParsedArgs::value(const std::string& name) const {
    auto it = values.find(name);
    if (it == values.end()) return std::nullopt;
    return it->second;
}

core::Result<ParsedArgs> parseArgs(const std::vector<std::string>& args, const std::vector<OptionSpec>& specs) {
    ParsedArgs parsed;
    bool optionsEnded = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (optionsEnded || a.size() < 2 || a[0] != '-' || (a[1] != '-' && a != "-h")) {
            // A lone "-" or a negative number such as "-3" is a positional value.
            parsed.positional.push_back(a);
            continue;
        }
        if (a == "--") {
            optionsEnded = true;
            continue;
        }
        if (a == "--help" || a == "-h") {
            parsed.help = true;
            continue;
        }
        std::string name = a.substr(2);
        std::optional<std::string> inlineValue;
        if (auto eq = name.find('='); eq != std::string::npos) {
            inlineValue = name.substr(eq + 1);
            name = name.substr(0, eq);
        }
        const auto spec = std::find_if(specs.begin(), specs.end(), [&](const OptionSpec& s) { return s.name == name; });
        if (spec == specs.end())
            return core::makeError(core::ErrorCode::InvalidArgument, "unknown option '--" + name + "'.");
        if (!spec->takesValue) {
            if (inlineValue)
                return core::makeError(core::ErrorCode::InvalidArgument, "option '--" + name + "' takes no value.");
            parsed.flags.insert(name);
            continue;
        }
        if (inlineValue) {
            parsed.values[name] = *inlineValue;
        } else if (i + 1 < args.size()) {
            parsed.values[name] = args[++i];
        } else {
            return core::makeError(core::ErrorCode::InvalidArgument, "option '--" + name + "' needs a value.");
        }
    }
    return parsed;
}

int usageError(Io& io, std::string_view command, std::string_view message) {
    io.err << "fos " << command << ": " << message << "\n"
           << "Run 'fos " << command << " --help' for usage.\n";
    return kExitUsageOrIo;
}

std::optional<project::LoadedProject> loadProjectOrReport(Io& io, std::string_view command, const std::string& path) {
    auto loaded = project::loadProject(path);
    if (!loaded) {
        io.err << "fos " << command << ": cannot load '" << path << "': " << loaded.error().message << "\n";
        return std::nullopt;
    }
    return std::move(loaded).value();
}

std::string resolveErrorNotice(std::string_view command, const project::LoadedProject& loaded) {
    const auto& resolved = loaded.resolved;
    if (!core::hasErrors(resolved.diagnostics)) return {};
    std::vector<std::string> missing;
    for (const auto& item : resolved.items)
        if (!item.generated) missing.push_back(item.itemId);
    std::ostringstream s;
    s << "fos " << command << ": INCOMPLETE OUTPUT: the project has " << errorCount(resolved.diagnostics)
      << " model error(s).";
    if (missing.empty()) {
        s << " All items were generated, but the output is built from a model with errors.\n";
    } else {
        s << " Item(s) that could not be generated and are MISSING from this output:";
        for (std::size_t i = 0; i < missing.size(); ++i) s << (i ? ", " : " ") << missing[i];
        s << ".\n";
    }
    core::Diagnostics errors;
    for (const auto& d : resolved.diagnostics)
        if (d.severity == core::Severity::Error) errors.push_back(d);
    printDiagnostics(s, errors);
    return s.str();
}

bool writeOutputFile(Io& io, std::string_view command, const std::filesystem::path& path, std::string_view content) {
    formats::SaveOptions options;
    options.backupCount = 0;
    std::error_code ec;
    const auto parent = path.parent_path();
    if (!parent.empty() && !std::filesystem::is_directory(parent, ec)) {
        io.err << "fos " << command << ": cannot write '" << path.string() << "': directory '" << parent.string()
               << "' does not exist.\n";
        return false;
    }
    auto r = formats::atomicWriteFile(path, content, options);
    if (!r) {
        io.err << "fos " << command << ": cannot write '" << path.string() << "': " << r.error().message << "\n";
        return false;
    }
    return true;
}

std::optional<double> parseNumber(std::string_view text) {
    if (text.empty()) return std::nullopt;
    double v = 0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    if (*first == '+') ++first;
    auto [ptr, ec] = std::from_chars(first, last, v);
    if (ec != std::errc() || ptr != last || !std::isfinite(v)) return std::nullopt;
    return v;
}

const char* severityText(core::Severity severity) noexcept {
    switch (severity) {
    case core::Severity::Error: return "error";
    case core::Severity::Warning: return "warning";
    case core::Severity::Info: return "info";
    }
    return "unknown";
}

void printDiagnostics(std::ostream& out, const core::Diagnostics& diagnostics, std::string_view indent) {
    for (const auto& d : diagnostics) {
        out << indent << "[" << severityText(d.severity) << "] " << d.code;
        if (!d.entityId.empty()) out << " (" << d.entityId << ")";
        out << ": " << d.message << "\n";
    }
}

std::size_t errorCount(const core::Diagnostics& diagnostics) noexcept {
    return static_cast<std::size_t>(std::count_if(diagnostics.begin(), diagnostics.end(),
                                                  [](const auto& d) { return d.severity == core::Severity::Error; }));
}

std::size_t warningCount(const core::Diagnostics& diagnostics) noexcept {
    return static_cast<std::size_t>(std::count_if(diagnostics.begin(), diagnostics.end(),
                                                  [](const auto& d) { return d.severity == core::Severity::Warning; }));
}

namespace {
/// Number of code points (UTF-8 continuation bytes are not counted).
std::size_t displayWidth(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}
} // namespace

std::string textTable(const production::Table& table) {
    std::vector<std::size_t> widths(table.header.size(), 0);
    auto widen = [&](const std::vector<std::string>& row) {
        if (row.size() > widths.size()) widths.resize(row.size(), 0);
        for (std::size_t i = 0; i < row.size(); ++i) widths[i] = std::max(widths[i], displayWidth(row[i]));
    };
    widen(table.header);
    for (const auto& row : table.rows) widen(row);
    std::ostringstream os;
    auto line = [&](const std::vector<std::string>& row) {
        std::string text;
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i > 0) text += "  ";
            text += row[i];
            if (i + 1 < row.size()) text.append(widths[i] - displayWidth(row[i]), ' ');
        }
        while (!text.empty() && text.back() == ' ') text.pop_back();
        os << text << "\n";
    };
    line(table.header);
    std::size_t total = 0;
    for (std::size_t i = 0; i < widths.size(); ++i) total += widths[i] + (i > 0 ? 2 : 0);
    os << std::string(total, '-') << "\n";
    for (const auto& row : table.rows) line(row);
    return os.str();
}

std::string safeFileName(std::string_view id) {
    std::string s(id);
    for (char& c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                        c == '_' || c == '-';
        if (!ok) c = '_';
    }
    if (s.empty()) s = "_";
    return s;
}

} // namespace fos::cli::detail
