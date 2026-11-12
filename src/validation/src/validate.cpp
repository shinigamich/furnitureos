// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/validation/validation.hpp"

#include "detail.hpp"

#include <algorithm>
#include <array>
#include <tuple>

namespace fos::validation {
namespace {

/// The registry: every check, in execution order. Output order does not depend on it
/// (the report is sorted), but stats such as checksRun do.
const std::array<detail::Check, 7>& registry() {
    static const std::array<detail::Check, 7> checks{{
        {"parts", "Part sizes, materials, ids, placements and fit on the stock sheet (grain aware).",
         &ValidationOptions::checkParts, &detail::checkParts},
        {"edge-banding", "Edge band references, band width and thickness, cut size after banding.",
         &ValidationOptions::checkEdgeBanding, &detail::checkEdgeBanding},
        {"part-interference", "Exact box/box interference of the parts of each assembly, grooves recognised.",
         &ValidationOptions::checkPartInterference, &detail::checkPartInterference},
        {"assembly-interference", "Interference between parts of different assemblies in the project frame.",
         &ValidationOptions::checkAssemblyInterference, &detail::checkAssemblyInterference},
        {"machining", "Per-part machining validation and coaxiality of mating holes on different parts.",
         &ValidationOptions::checkMachining, &detail::checkMachining},
        {"hardware", "Hardware instances refer to known definitions and parts, positive quantities.",
         &ValidationOptions::checkHardware, &detail::checkHardware},
        {"completeness", "Names, empty assemblies, material sheet data and costs.",
         &ValidationOptions::checkCompleteness, &detail::checkCompleteness},
    }};
    return checks;
}

int rank(core::Severity s) noexcept {
    switch (s) {
    case core::Severity::Error: return 0;
    case core::Severity::Warning: return 1;
    case core::Severity::Info: return 2;
    }
    return 3;
}
} // namespace

ValidationReport validateModel(const domain::Model& model, const ValidationOptions& options) {
    ValidationReport report;
    ValidationStats& st = report.stats;
    st.assemblies = model.assemblies.size();
    for (const auto& a : model.assemblies) {
        st.parts += a.parts.size();
        st.hardwareInstances += a.hardware.size();
        for (const auto& p : a.parts) st.operations += p.operations.size();
    }

    detail::Context ctx{model, options, report};
    for (const detail::Check& check : registry()) {
        if (!(options.*check.enabled)) continue;
        check.run(ctx);
        ++st.checksRun;
    }

    auto key = [](const Finding& f) {
        return std::tie(f.assemblyId, f.partId, f.code, f.message, f.entityId);
    };
    std::stable_sort(report.diagnostics.begin(), report.diagnostics.end(), [&](const Finding& a, const Finding& b) {
        if (rank(a.severity) != rank(b.severity)) return rank(a.severity) < rank(b.severity);
        return key(a) < key(b);
    });
    for (const Finding& f : report.diagnostics) {
        if (f.severity == core::Severity::Error) ++report.counts.errors;
        else if (f.severity == core::Severity::Warning) ++report.counts.warnings;
        else ++report.counts.infos;
    }
    return report;
}

std::vector<CheckInfo> availableChecks() {
    std::vector<CheckInfo> out;
    for (const detail::Check& c : registry()) out.push_back({c.name, c.description});
    return out;
}

} // namespace fos::validation
