// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/model.hpp"
#include "fos/validation/options.hpp"
#include "fos/validation/report.hpp"

#include <string>
#include <vector>

/// Model-level validation. Codes, severities and options: src/validation/README.md.
namespace fos::validation {

/// Runs every enabled check on `model` (read-only) and returns the sorted findings.
/// Never throws and never crashes on nonsensical input (NaN sizes, dangling ids, ...).
ValidationReport validateModel(const domain::Model& model, const ValidationOptions& options = {});

struct CheckInfo {
    std::string name;        ///< e.g. "part-interference"
    std::string description; ///< One sentence.
};
/// The registered checks in execution order.
std::vector<CheckInfo> availableChecks();

} // namespace fos::validation
