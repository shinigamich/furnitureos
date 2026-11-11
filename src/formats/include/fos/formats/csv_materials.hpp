// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/domain/material.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

/// CSV import of a material list (e.g. a supplier price list or a spreadsheet export).
/// The exact rules are specified in src/formats/README.md ("CSV material list").
namespace fos::formats {

struct CsvMaterialImportOptions {
    /// Field separator: ';' or ','. 0 = detect from the header line (';' if the header
    /// contains a semicolon outside quotes, otherwise ',').
    char separator = 0;
    /// Supplies ids for rows without an `id` column/value. Null = random UUID v4.
    std::function<std::string()> newId;
};

struct CsvMaterialImport {
    std::vector<domain::Material> materials; ///< In file order.
    char separator = ';';                    ///< Separator that was used.
};

/// Parses CSV text (UTF-8, optional BOM, LF or CRLF line ends).
///  * Line 1 is the header; columns are matched by name, case-insensitively, in any
///    order: `name` and `thickness` are required; `sheetLength`, `sheetWidth`, `kind`,
///    `costPerSheet`, `hasGrain`, `id`, `manufacturer`, `code`, `supplier` are optional.
///    Any other column is kept per material as custom[<header text>] (empty cells skipped).
///  * Numbers (mm; cost in project currency): digits with an optional sign and ONE
///    decimal mark, '.' or ','. No thousands separators, no exponent. With the ','
///    separator a decimal comma only works inside a quoted field ("18,5").
///  * Fields may be quoted with '"' ('""' = literal quote); a quoted field cannot span lines.
///  * Empty cells: sheetLength 2800, sheetWidth 2070, kind "other", costPerSheet 0,
///    hasGrain false. hasGrain accepts true/false, yes/no, 1/0 (any case).
///  * Blank lines are skipped.
/// All-or-nothing: any invalid row fails the import (ParseError); the message lists
/// every problem with its 1-based line number ("row 3, column 'thickness': ...").
core::Result<CsvMaterialImport> importMaterialsCsv(std::string_view text,
                                                   const CsvMaterialImportOptions& options = {});

/// Reads `path` and calls importMaterialsCsv.
core::Result<CsvMaterialImport> importMaterialsCsvFile(const std::filesystem::path& path,
                                                       const CsvMaterialImportOptions& options = {});

} // namespace fos::formats
