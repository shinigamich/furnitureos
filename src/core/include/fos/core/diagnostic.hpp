// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <string>
#include <vector>

namespace fos::core {

enum class Severity { Info, Warning, Error };

/// A validation finding. Messages must be specific and actionable, e.g.
/// "Right side panel exceeds the internal cabinet width by 3 mm", never "Invalid project".
struct Diagnostic {
    Severity severity = Severity::Error;
    std::string code;     ///< Stable machine-readable code, e.g. "machining.hole.too_deep".
    std::string message;  ///< Human-readable English sentence.
    std::string entityId; ///< Id of the part/assembly/parameter concerned (may be empty).
};

using Diagnostics = std::vector<Diagnostic>;

inline bool hasErrors(const Diagnostics& ds) noexcept {
    for (const auto& d : ds)
        if (d.severity == Severity::Error) return true;
    return false;
}

} // namespace fos::core
