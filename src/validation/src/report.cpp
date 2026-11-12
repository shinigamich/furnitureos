// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/validation/report.hpp"

#include <algorithm>
#include <sstream>

namespace fos::validation {

core::Diagnostics ValidationReport::toDiagnostics() const {
    core::Diagnostics out;
    out.reserve(diagnostics.size());
    for (const Finding& f : diagnostics) out.push_back(static_cast<const core::Diagnostic&>(f));
    return out;
}

std::vector<Finding> findingsForEntity(const ValidationReport& report, std::string_view id) {
    std::vector<Finding> out;
    for (const Finding& f : report.diagnostics)
        if (f.entityId == id || f.assemblyId == id || f.partId == id ||
            std::find(f.relatedIds.begin(), f.relatedIds.end(), id) != f.relatedIds.end())
            out.push_back(f);
    return out;
}

std::vector<Finding> findingsWithCode(const ValidationReport& report, std::string_view code) {
    std::vector<Finding> out;
    for (const Finding& f : report.diagnostics)
        if (f.code == code) out.push_back(f);
    return out;
}

std::vector<Finding> findingsWithSeverity(const ValidationReport& report, core::Severity severity) {
    std::vector<Finding> out;
    for (const Finding& f : report.diagnostics)
        if (f.severity == severity) out.push_back(f);
    return out;
}

const char* toString(core::Severity severity) noexcept {
    switch (severity) {
    case core::Severity::Error: return "error";
    case core::Severity::Warning: return "warning";
    case core::Severity::Info: return "info";
    }
    return "unknown";
}

namespace {
std::string summary(const ValidationReport& r) {
    std::ostringstream s;
    s << (r.ok() ? "Validation passed" : "Validation FAILED") << ": " << r.counts.errors << " error(s), "
      << r.counts.warnings << " warning(s), " << r.counts.infos << " info(s); " << r.stats.parts << " part(s) in "
      << r.stats.assemblies << " assembly(ies), " << r.stats.checksRun << " check(s) run.";
    return s.str();
}

std::string cell(const std::string& text) {
    std::string out;
    for (char ch : text) {
        if (ch == '|') out += "\\|";
        else if (ch == '\n') out += ' ';
        else out += ch;
    }
    return out;
}
} // namespace

std::string toText(const ValidationReport& report) {
    std::string out = summary(report) + "\n";
    for (const Finding& f : report.diagnostics) {
        out += "[" + std::string(toString(f.severity)) + "] " + f.code;
        if (!f.entityId.empty()) out += " (" + f.entityId + ")";
        out += ": " + f.message + "\n";
    }
    return out;
}

std::string toMarkdown(const ValidationReport& report) {
    std::ostringstream s;
    s << "# Validation report\n\n" << summary(report) << "\n";
    const struct {
        core::Severity severity;
        const char* title;
    } sections[] = {{core::Severity::Error, "Errors"},
                    {core::Severity::Warning, "Warnings"},
                    {core::Severity::Info, "Information"}};
    for (const auto& sec : sections) {
        const auto list = findingsWithSeverity(report, sec.severity);
        if (list.empty()) continue;
        s << "\n## " << sec.title << " (" << list.size() << ")\n\n"
          << "| Code | Assembly | Part | Entity | Message |\n|---|---|---|---|---|\n";
        for (const Finding& f : list)
            s << "| `" << cell(f.code) << "` | " << cell(f.assemblyId) << " | " << cell(f.partId) << " | "
              << cell(f.entityId) << " | " << cell(f.message) << " |\n";
    }
    const ValidationStats& st = report.stats;
    s << "\n## Statistics\n\n| Counter | Value |\n|---|---|\n"
      << "| Assemblies | " << st.assemblies << " |\n"
      << "| Parts | " << st.parts << " |\n"
      << "| Operations | " << st.operations << " |\n"
      << "| Hardware instances | " << st.hardwareInstances << " |\n"
      << "| Checks run | " << st.checksRun << " |\n"
      << "| Possible part pairs | " << st.possiblePartPairs << " |\n"
      << "| Broad-phase checks | " << st.broadPhaseChecks << " |\n"
      << "| Part pairs tested (SAT) | " << st.partPairsTested << " |\n"
      << "| Groove engagements allowed | " << st.grooveEngagementsAllowed << " |\n"
      << "| Possible cross-assembly part pairs | " << st.possibleCrossAssemblyPartPairs << " |\n"
      << "| Cross-assembly broad-phase checks | " << st.crossAssemblyBroadPhaseChecks << " |\n"
      << "| Cross-assembly part pairs tested | " << st.crossAssemblyPartPairsTested << " |\n"
      << "| Mating hole pairs | " << st.matingHolePairs << " |\n";
    return s.str();
}

} // namespace fos::validation
