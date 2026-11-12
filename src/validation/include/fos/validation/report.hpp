// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace fos::validation {

/// A diagnostic plus the model location it belongs to. `entityId` (inherited) is the
/// primary entity: a part id, assembly id, material id, edge band id, hardware instance
/// id or hardware definition id depending on the code (see README).
struct Finding : core::Diagnostic {
    std::string assemblyId;              ///< Owning assembly, empty for model-level findings.
    std::string partId;                  ///< Part concerned, empty for non-part findings.
    std::vector<std::string> relatedIds; ///< Other entities involved (second part, material, ...).
};

struct SeverityCounts {
    std::size_t errors = 0;
    std::size_t warnings = 0;
    std::size_t infos = 0;
};

/// Work counters of one validateModel() run.
struct ValidationStats {
    std::size_t assemblies = 0;
    std::size_t parts = 0;
    std::size_t operations = 0;
    std::size_t hardwareInstances = 0;
    std::size_t checksRun = 0;
    /// Part interference within assemblies: sum over assemblies of n*(n-1)/2 ...
    std::size_t possiblePartPairs = 0;
    /// ... pairs that reached the bounding-box comparison of the sweep (broad phase) ...
    std::size_t broadPhaseChecks = 0;
    /// ... and pairs whose bounding boxes overlapped, i.e. exact SAT tests performed.
    std::size_t partPairsTested = 0;
    /// Pairs whose boxes overlap but only inside a groove (accepted engagements).
    std::size_t grooveEngagementsAllowed = 0;
    /// Same counters for part pairs of DIFFERENT assemblies (project frame).
    std::size_t possibleCrossAssemblyPartPairs = 0;
    std::size_t crossAssemblyBroadPhaseChecks = 0;
    std::size_t crossAssemblyPartPairsTested = 0;
    /// Hole pairs classified as mating pairs by the coaxiality check.
    std::size_t matingHolePairs = 0;
};

struct ValidationReport {
    /// Sorted: severity (Error, Warning, Info), then assemblyId, partId, code, message, entityId.
    std::vector<Finding> diagnostics;
    SeverityCounts counts;
    ValidationStats stats;

    /// True when there is no Error (warnings and infos allowed).
    bool ok() const noexcept { return counts.errors == 0; }
    /// Plain core diagnostics in the same order.
    core::Diagnostics toDiagnostics() const;
};

/// Findings that concern `id`: it is the entityId, assemblyId, partId or one of relatedIds.
std::vector<Finding> findingsForEntity(const ValidationReport& report, std::string_view id);
/// Findings with exactly this code.
std::vector<Finding> findingsWithCode(const ValidationReport& report, std::string_view code);
/// Findings with exactly this severity.
std::vector<Finding> findingsWithSeverity(const ValidationReport& report, core::Severity severity);

const char* toString(core::Severity severity) noexcept; ///< "error" / "warning" / "info"

/// Plain-text report: a summary line followed by one line per finding
/// ("[error] code (entity): message").
std::string toText(const ValidationReport& report);
/// Markdown report: summary, one table per severity, statistics.
std::string toMarkdown(const ValidationReport& report);

} // namespace fos::validation
