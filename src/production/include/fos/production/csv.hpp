// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/production/common.hpp"

#include <filesystem>
#include <string_view>

namespace fos::production {

struct CsvOptions {
    char separator = ',';   ///< ',' or ';' (any single-byte character except '"', CR, LF).
    bool utf8Bom = false;   ///< Prefix EF BB BF (helps Excel detect UTF-8).
    bool crlf = true;       ///< RFC 4180 line ending CRLF; false -> LF.
    bool writeHeader = true;
};

/// RFC 4180 field quoting: a field is wrapped in double quotes when it contains the
/// separator, a double quote, CR or LF, or has leading/trailing spaces; embedded
/// double quotes are doubled. Bytes are otherwise passed through (UTF-8 safe).
std::string csvField(std::string_view value, char separator = ',');

/// Serialises a table as UTF-8 CSV; every record (including the last) ends with the
/// line ending. Rows shorter than the header are NOT padded.
/// Note: cells beginning with '=', '+', '-' or '@' are written unchanged (no
/// spreadsheet formula neutralisation).
std::string toCsv(const Table& table, const CsvOptions& options = {});

/// Writes `content` to `path` in binary mode (no newline translation).
/// IoError with the path in the message when the file cannot be created or written.
core::Result<void> writeTextFile(const std::filesystem::path& path, std::string_view content);

} // namespace fos::production
