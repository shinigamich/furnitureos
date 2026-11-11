// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/formats/csv_materials.hpp"

#include "fos/formats/container.hpp"
#include "fos/formats/enums.hpp"

#include <algorithm>
#include <cmath>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <sstream>

namespace fos::formats {
namespace {

using core::ErrorCode;
using core::makeError;

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t");
    return std::string(text.substr(first, last - first + 1));
}

std::string lower(std::string text) {
    for (char& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

/// Splits one line. Returns nullopt for an unterminated or malformed quoted field.
std::optional<std::vector<std::string>> splitLine(std::string_view line, char separator) {
    std::vector<std::string> fields;
    std::size_t i = 0;
    while (true) {
        std::size_t start = i;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) ++start;
        std::string field;
        if (start < line.size() && line[start] == '"') {
            i = start + 1;
            bool closed = false;
            while (i < line.size()) {
                if (line[i] == '"') {
                    if (i + 1 < line.size() && line[i + 1] == '"') {
                        field.push_back('"');
                        i += 2;
                        continue;
                    }
                    closed = true;
                    ++i;
                    break;
                }
                field.push_back(line[i++]);
            }
            if (!closed) return std::nullopt;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
            if (i < line.size() && line[i] != separator) return std::nullopt; // text after the closing quote
        } else {
            const std::size_t end = std::min(line.find(separator, i), line.size());
            field = trim(line.substr(i, end - i));
            i = end;
        }
        fields.push_back(std::move(field));
        if (i >= line.size()) break;
        ++i; // skip the separator; a trailing separator yields a final empty field
        if (i == line.size()) {
            fields.emplace_back();
            break;
        }
    }
    return fields;
}

/// [sign] digits [('.' | ',') digits], at least one digit. Locale-independent.
std::optional<double> parseNumber(const std::string& raw) {
    std::string text = trim(raw);
    std::size_t i = (!text.empty() && (text[0] == '+' || text[0] == '-')) ? 1 : 0;
    std::size_t digits = 0, marks = 0;
    for (; i < text.size(); ++i) {
        if (text[i] >= '0' && text[i] <= '9') {
            ++digits;
        } else if ((text[i] == '.' || text[i] == ',') && marks == 0) {
            ++marks;
            text[i] = '.';
        } else {
            return std::nullopt;
        }
    }
    if (digits == 0) return std::nullopt;
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    double value = 0;
    stream >> value;
    if (stream.fail() || !std::isfinite(value)) return std::nullopt;
    return value;
}

std::optional<bool> parseBool(const std::string& raw) {
    const std::string text = lower(trim(raw));
    if (text == "true" || text == "yes" || text == "1") return true;
    if (text == "false" || text == "no" || text == "0") return false;
    return std::nullopt;
}

enum class Column { Name, Thickness, SheetLength, SheetWidth, Kind, CostPerSheet, HasGrain, Id, Manufacturer, Code, Supplier, Custom };

Column columnFor(const std::string& header) {
    static const std::map<std::string, Column> known{
        {"name", Column::Name},           {"thickness", Column::Thickness},       {"sheetlength", Column::SheetLength},
        {"sheetwidth", Column::SheetWidth}, {"kind", Column::Kind},               {"costpersheet", Column::CostPerSheet},
        {"hasgrain", Column::HasGrain},   {"id", Column::Id},                     {"manufacturer", Column::Manufacturer},
        {"code", Column::Code},           {"supplier", Column::Supplier}};
    const auto it = known.find(lower(header));
    return it == known.end() ? Column::Custom : it->second;
}

char detectSeparator(std::string_view header) {
    bool quoted = false;
    for (const char c : header) {
        if (c == '"') quoted = !quoted;
        else if (c == ';' && !quoted) return ';';
    }
    return ',';
}

} // namespace

core::Result<CsvMaterialImport> importMaterialsCsv(std::string_view text, const CsvMaterialImportOptions& options) {
    if (options.separator != 0 && options.separator != ';' && options.separator != ',')
        return makeError(ErrorCode::InvalidArgument, "Material CSV: the separator must be ';' or ','.");
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);

    // Split into lines, remembering 1-based line numbers; skip blank lines.
    std::vector<std::pair<std::size_t, std::string_view>> lines;
    for (std::size_t pos = 0, number = 1; pos <= text.size(); ++number) {
        const std::size_t end = std::min(text.find('\n', pos), text.size());
        std::string_view line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.find_first_not_of(" \t") != std::string_view::npos) lines.emplace_back(number, line);
        pos = end + 1;
    }
    if (lines.empty()) return makeError(ErrorCode::ParseError, "Material CSV is empty: the header row is missing.");

    CsvMaterialImport out;
    out.separator = options.separator != 0 ? options.separator : detectSeparator(lines[0].second);
    const std::string separatorText = std::string("'") + out.separator + "'";
    const std::string headerRow = "row " + std::to_string(lines[0].first);

    // --- header
    const auto header = splitLine(lines[0].second, out.separator);
    if (!header) return makeError(ErrorCode::ParseError, "Material CSV " + headerRow + " (header): unterminated or malformed quoted field.");
    std::vector<Column> columns;
    std::set<std::string> seenHeaders;
    for (std::size_t i = 0; i < header->size(); ++i) {
        const std::string& title = (*header)[i];
        if (title.empty())
            return makeError(ErrorCode::ParseError, "Material CSV " + headerRow + " (header): column " +
                                                        std::to_string(i + 1) + " has no name.");
        if (!seenHeaders.insert(lower(title)).second)
            return makeError(ErrorCode::ParseError,
                             "Material CSV " + headerRow + " (header): column '" + title + "' appears more than once.");
        columns.push_back(columnFor(title));
    }
    for (const auto& [column, title] : {std::pair{Column::Name, "name"}, std::pair{Column::Thickness, "thickness"}})
        if (std::find(columns.begin(), columns.end(), column) == columns.end())
            return makeError(ErrorCode::ParseError,
                             "Material CSV " + headerRow + " (header): the required column '" + title +
                                 "' is missing (separator " + separatorText + "; columns found: " +
                                 std::to_string(columns.size()) + ").");

    // --- rows
    std::vector<std::string> problems;
    std::map<std::string, std::size_t> idRows;
    for (std::size_t l = 1; l < lines.size(); ++l) {
        const std::string row = "row " + std::to_string(lines[l].first);
        const auto fields = splitLine(lines[l].second, out.separator);
        if (!fields) {
            problems.push_back(row + ": unterminated or malformed quoted field.");
            continue;
        }
        if (fields->size() != columns.size()) {
            problems.push_back(row + ": expected " + std::to_string(columns.size()) + " fields but found " +
                               std::to_string(fields->size()) + " (separator " + separatorText + ")" +
                               (out.separator == ',' && fields->size() > columns.size()
                                    ? "; a number with a decimal comma must be quoted, or use ';' as the separator."
                                    : "."));
            continue;
        }
        domain::Material material;
        material.kind = domain::MaterialKind::Other;
        bool valid = true;
        const auto bad = [&](const std::string& title, const std::string& expected, const std::string& got) {
            problems.push_back(row + ", column '" + title + "': expected " + expected + ", got '" + got + "'.");
            valid = false;
        };
        const auto positive = [&](const std::string& title, const std::string& cell, double& target, bool required,
                                  bool allowZero) {
            if (cell.empty() && !required) return;
            const auto value = parseNumber(cell);
            if (!value || *value < 0 || (!allowZero && *value == 0))
                return bad(title, allowZero ? "a non-negative number" : "a positive number", cell);
            target = *value;
        };
        for (std::size_t c = 0; c < columns.size(); ++c) {
            const std::string& title = (*header)[c];
            const std::string cell = trim((*fields)[c]);
            switch (columns[c]) {
            case Column::Name:
                if (cell.empty()) bad(title, "a material name", cell);
                material.name = cell;
                break;
            case Column::Thickness: positive(title, cell, material.thickness, true, false); break;
            case Column::SheetLength: positive(title, cell, material.sheetLength, false, false); break;
            case Column::SheetWidth: positive(title, cell, material.sheetWidth, false, false); break;
            case Column::CostPerSheet: positive(title, cell, material.costPerSheet, false, true); break;
            case Column::Kind: {
                if (cell.empty()) break;
                std::string token = lower(cell);
                std::replace(token.begin(), token.end(), ' ', '_');
                std::replace(token.begin(), token.end(), '-', '_');
                if (const auto kind = enumFromToken<domain::MaterialKind>(token)) material.kind = *kind;
                else bad(title, "one of " + allowedTokens<domain::MaterialKind>(), cell);
                break;
            }
            case Column::HasGrain: {
                if (cell.empty()) break;
                if (const auto flag = parseBool(cell)) material.hasGrain = *flag;
                else bad(title, "true/false, yes/no or 1/0", cell);
                break;
            }
            case Column::Id: material.id.value = cell; break;
            case Column::Manufacturer: material.manufacturer = cell; break;
            case Column::Code: material.code = cell; break;
            case Column::Supplier: material.supplier = cell; break;
            case Column::Custom:
                if (!cell.empty()) material.custom[title] = cell;
                break;
            }
        }
        if (!material.id.empty()) {
            const auto [it, inserted] = idRows.emplace(material.id.str(), lines[l].first);
            if (!inserted) {
                problems.push_back(row + ", column 'id': the id '" + material.id.str() + "' is already used in row " +
                                   std::to_string(it->second) + ".");
                valid = false;
            }
        }
        if (valid) out.materials.push_back(std::move(material));
    }
    if (!problems.empty()) {
        std::string message = "Material CSV has " + std::to_string(problems.size()) +
                              (problems.size() == 1 ? " problem" : " problems") + "; nothing was imported:";
        const std::size_t shown = std::min<std::size_t>(problems.size(), 50);
        for (std::size_t i = 0; i < shown; ++i) message += "\n" + problems[i];
        if (shown < problems.size()) message += "\n... and " + std::to_string(problems.size() - shown) + " more.";
        return makeError(ErrorCode::ParseError, message);
    }

    // --- ids for rows without one (must not collide with ids given in the file)
    for (domain::Material& material : out.materials) {
        if (!material.id.empty()) continue;
        std::string fresh;
        for (int attempt = 0; attempt < 100 && fresh.empty(); ++attempt) {
            std::string candidate = options.newId ? options.newId() : core::newUuid();
            if (!candidate.empty() && idRows.emplace(candidate, 0).second) fresh = std::move(candidate);
        }
        if (fresh.empty())
            return makeError(ErrorCode::Internal,
                             "Material CSV: the id generator returned 100 empty or already used ids for '" +
                                 material.name + "'.");
        material.id.value = fresh;
    }
    return out;
}

core::Result<CsvMaterialImport> importMaterialsCsvFile(const std::filesystem::path& path,
                                                       const CsvMaterialImportOptions& options) {
    auto text = readFile(path);
    if (!text) return text.error();
    auto imported = importMaterialsCsv(text.value(), options);
    if (!imported) {
        core::Error error = imported.error();
        error.context = path.string();
        return error;
    }
    return imported;
}

} // namespace fos::formats
