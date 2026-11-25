// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal helpers of the project diff (not installed).
#include "fos/project/diff.hpp"

#include <optional>
#include <string>
#include <vector>

namespace fos::project::detail {

/// Appends the differences between `a` and `b` to `out` (path syntax: diff.hpp ValueChange).
void diffJson(const formats::Json& a, const formats::Json& b, const std::string& path, double tolerance,
              std::vector<ValueChange>& out);

/// Number with up to 12 significant digits and no trailing zeros ("800", "296.5").
std::string numberText(double value);

/// Display text of a value: "(none)" for nullopt, numbers via numberText, strings quoted,
/// other JSON compact (longer than 80 characters: shortened with "...").
std::string valueText(const std::optional<formats::Json>& value);

} // namespace fos::project::detail
