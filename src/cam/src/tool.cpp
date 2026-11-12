// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/cam/tool.hpp"

#include "fos/core/units.hpp"

#include <algorithm>

namespace fos::cam {

using core::ErrorCode;
using core::formatLength;
using core::makeError;

const char* toString(ToolKind k) noexcept {
    switch (k) {
    case ToolKind::Drill: return "drill";
    case ToolKind::EndMill: return "end mill";
    }
    return "unknown";
}

namespace {
std::string mm(double v) { return formatLength(v, 3); }

std::string label(const Tool& t) {
    return std::string(t.kind == ToolKind::Drill ? "Drill" : "End mill") + " '" + t.id + "' (T" +
           std::to_string(t.number) + ", " + mm(t.diameter) + " mm)";
}

std::string availableDiameters(const std::vector<Tool>& tools, ToolKind kind) {
    std::vector<double> ds;
    for (const auto& t : tools)
        if (t.kind == kind) ds.push_back(t.diameter);
    std::sort(ds.begin(), ds.end());
    std::string s;
    double last = -1;
    for (double d : ds) {
        if (!s.empty() && core::approxEqual(d, last)) continue;
        if (!s.empty()) s += ", ";
        s += mm(d);
        last = d;
    }
    return s.empty() ? "none" : s;
}
} // namespace

core::Result<void> ToolLibrary::add(Tool t) {
    const std::string who = "Tool '" + t.id + "'";
    if (t.id.empty()) return makeError(ErrorCode::InvalidArgument, "A tool needs a non-empty id.");
    if (findById(t.id)) return makeError(ErrorCode::AlreadyExists, who + " is already in the tool library.", t.id);
    if (t.number < 1)
        return makeError(ErrorCode::InvalidArgument, who + " has tool number " + std::to_string(t.number) +
                                                         "; tool numbers must be >= 1.", t.id);
    if (const Tool* o = findByNumber(t.number))
        return makeError(ErrorCode::AlreadyExists, who + " uses tool number T" + std::to_string(t.number) +
                                                       ", which is already taken by '" + o->id + "'.", t.id);
    auto positive = [&](double v, const char* what) -> std::string {
        if (v > 0) return {};
        return who + " has " + what + " " + formatLength(v, 3) + "; it must be greater than 0.";
    };
    for (const auto& msg : {positive(t.diameter, "diameter"), positive(t.fluteLength, "flute length"),
                            positive(t.feedRate, "feed rate"), positive(t.plungeRate, "plunge rate"),
                            positive(t.rpm, "spindle speed"),
                            t.kind == ToolKind::EndMill ? positive(t.maxDepthOfCut, "max depth of cut")
                                                        : std::string{}})
        if (!msg.empty()) return makeError(ErrorCode::InvalidArgument, msg, t.id);
    auto pos = std::lower_bound(tools_.begin(), tools_.end(), t.number,
                                [](const Tool& a, int n) { return a.number < n; });
    tools_.insert(pos, std::move(t));
    return {};
}

const Tool* ToolLibrary::findByNumber(int number) const noexcept {
    for (const auto& t : tools_)
        if (t.number == number) return &t;
    return nullptr;
}

const Tool* ToolLibrary::findById(std::string_view id) const noexcept {
    for (const auto& t : tools_)
        if (t.id == id) return &t;
    return nullptr;
}

core::Result<const Tool*> ToolLibrary::selectDrill(double diameter, double requiredDepth) const {
    const Tool* tooShort = nullptr;
    for (const auto& t : tools_) { // sorted by number -> lowest number first
        if (t.kind != ToolKind::Drill || !core::approxEqual(t.diameter, diameter)) continue;
        if (t.fluteLength + core::kLengthEps >= requiredDepth) return &t;
        if (!tooShort) tooShort = &t;
    }
    if (tooShort)
        return makeError(ErrorCode::NotFound, label(*tooShort) + " has flute length " + mm(tooShort->fluteLength) +
                                                  " mm but " + mm(requiredDepth) +
                                                  " mm of cutting depth is needed.", tooShort->id);
    return makeError(ErrorCode::NotFound, "No drill of diameter " + mm(diameter) +
                                              " mm in the tool library (available: " +
                                              availableDiameters(tools_, ToolKind::Drill) + ").");
}

core::Result<const Tool*> ToolLibrary::selectGrooveCutter(double grooveWidth, double requiredDepth) const {
    const Tool* best = nullptr;
    const Tool* tooShort = nullptr;
    for (const auto& t : tools_) {
        if (t.kind != ToolKind::EndMill || !t.centerCutting || t.diameter > grooveWidth + core::kLengthEps) continue;
        if (t.fluteLength + core::kLengthEps < requiredDepth) {
            if (!tooShort) tooShort = &t;
            continue;
        }
        if (!best || core::definitelyGreater(t.diameter, best->diameter)) best = &t;
    }
    if (best) return best;
    if (tooShort)
        return makeError(ErrorCode::NotFound, label(*tooShort) + " has flute length " + mm(tooShort->fluteLength) +
                                                  " mm but " + mm(requiredDepth) +
                                                  " mm of cutting depth is needed.", tooShort->id);
    return makeError(ErrorCode::NotFound, "No centre-cutting end mill with diameter <= " + mm(grooveWidth) +
                                              " mm in the tool library (end mill diameters available: " +
                                              availableDiameters(tools_, ToolKind::EndMill) + ").");
}

} // namespace fos::cam
