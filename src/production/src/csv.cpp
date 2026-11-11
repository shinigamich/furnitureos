// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/csv.hpp"

#include <fstream>

namespace fos::production {

std::string csvField(std::string_view value, char separator) {
    bool quote = !value.empty() && (value.front() == ' ' || value.back() == ' ');
    for (const char c : value)
        if (c == separator || c == '"' || c == '\r' || c == '\n') quote = true;
    if (!quote) return std::string(value);
    std::string out = "\"";
    for (const char c : value) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
    return out;
}

std::string toCsv(const Table& table, const CsvOptions& options) {
    // A separator that could not be told apart from quoting or record ends would
    // produce an unreadable file; fall back to the RFC 4180 comma.
    const char sep = (options.separator == '"' || options.separator == '\r' || options.separator == '\n' ||
                      options.separator == '\0')
                         ? ','
                         : options.separator;
    const char* eol = options.crlf ? "\r\n" : "\n";
    std::string out;
    if (options.utf8Bom) out += "\xEF\xBB\xBF";
    auto record = [&](const std::vector<std::string>& cells) {
        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (i) out += sep;
            out += csvField(cells[i], sep);
        }
        out += eol;
    };
    if (options.writeHeader) record(table.header);
    for (const auto& row : table.rows) record(row);
    return out;
}

core::Result<void> writeTextFile(const std::filesystem::path& path, std::string_view content) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
        return core::makeError(core::ErrorCode::IoError, "Cannot open '" + path.string() + "' for writing.",
                               path.string());
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    f.close();
    if (!f)
        return core::makeError(core::ErrorCode::IoError,
                               "Writing " + std::to_string(content.size()) + " bytes to '" + path.string() +
                                   "' failed.",
                               path.string());
    return {};
}

} // namespace fos::production
