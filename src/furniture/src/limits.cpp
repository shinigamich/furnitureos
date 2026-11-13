// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/furniture/limits.hpp"

#include "fos/core/units.hpp"
#include "fos/furniture/parameters.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace fos::furniture {
namespace {

std::string pathLabel(const std::string& path) { return path.empty() ? "root" : path; }
std::string childPath(const std::string& parent, std::size_t index) {
    return parent.empty() ? std::to_string(index + 1) : parent + "." + std::to_string(index + 1);
}
/// Message form of a number: formatLength for ordinary magnitudes, %g for huge or
/// non-finite values (formatLength is meant for lengths and cannot show 1e308).
std::string num(double v) {
    if (std::isfinite(v) && std::fabs(v) < 1e12) return core::formatLength(v, 3);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

std::size_t frontCount(const Section& s) {
    if (!s.front) return 0;
    switch (s.front->kind) {
    case FrontKind::Door: return 1;
    case FrontKind::DoorPair: return 2;
    case FrontKind::DrawerStack: return s.front->drawerHeights.size();
    }
    return 0;
}

core::Error limitError(const std::string& message, const std::string& owner) {
    return core::makeError(core::ErrorCode::ConstraintViolation, message, owner);
}

/// `what` e.g. "Section '2.1' of cabinet 'c'"; checks a Fixed / Weight size value.
core::Result<void> checkSize(const SectionSize& size, const std::string& what, const std::string& owner) {
    if (size.kind == SizeKind::Expression) return {};
    if (!std::isfinite(size.value))
        return core::makeError(core::ErrorCode::InvalidArgument, what + " has a non-finite size value.", owner);
    if (size.kind == SizeKind::Fixed && std::fabs(size.value) > limits::kMaxDimension)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               what + " has a fixed size of " + num(size.value) + " mm; the limit is " +
                                   num(limits::kMaxDimension) + " mm.",
                               owner);
    if (size.kind == SizeKind::Weight && std::fabs(size.value) > limits::kMaxWeight)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               what + " has a weight of " + num(size.value) + "; the limit is " +
                                   num(limits::kMaxWeight) + ".",
                               owner);
    return {};
}

struct Node {
    const Section* section;
    std::string path;
    std::size_t depth;
};

} // namespace

SectionTreeStats sectionTreeStats(const Section& root) {
    SectionTreeStats st;
    std::vector<std::pair<const Section*, std::size_t>> stack{{&root, 0}};
    while (!stack.empty()) {
        const auto [s, depth] = stack.back();
        stack.pop_back();
        ++st.sections;
        if (depth > st.maxDepth) st.maxDepth = depth;
        if (s->shelfCount > 0) st.shelves += static_cast<std::size_t>(s->shelfCount);
        if (s->children.size() > 1) st.dividers += s->children.size() - 1;
        st.fronts += frontCount(*s);
        for (const auto& c : s->children) stack.emplace_back(&c, depth + 1);
    }
    return st;
}

std::size_t estimatePartCount(const Section& root) {
    const SectionTreeStats st = sectionTreeStats(root);
    return limits::kMaxCarcassParts + st.dividers + st.shelves + st.fronts;
}

core::Result<void> checkSectionLimits(const Section& root, const std::string& owner) {
    using namespace limits;
    const std::string cab = "cabinet '" + owner + "'";
    std::size_t sections = 0, shelves = 0;
    // Pre-order, children in order: the first violation reported is deterministic.
    std::vector<Node> stack{{&root, std::string(), 0}};
    while (!stack.empty()) {
        Node n = std::move(stack.back());
        stack.pop_back();
        const Section& s = *n.section;
        const std::string label = "Section '" + pathLabel(n.path) + "' of " + cab;
        if (++sections > kMaxSections)
            return limitError("The interior of " + cab + " has more than " + std::to_string(kMaxSections) +
                                  " sections (limit " + std::to_string(kMaxSections) + "). Merge sections.",
                              owner);
        if (n.depth > kMaxSectionDepth)
            return limitError(label + " is nested " + std::to_string(n.depth) + " levels deep; the limit is " +
                                  std::to_string(kMaxSectionDepth) + " levels.",
                              owner);
        if (!n.path.empty())
            if (auto r = checkSize(s.size, label, owner); !r) return r;
        if (s.shelfCount > kMaxShelvesPerSection)
            return limitError(label + " has " + std::to_string(s.shelfCount) + " adjustable shelves; the limit is " +
                                  std::to_string(kMaxShelvesPerSection) + " per section.",
                              owner);
        if (s.shelfCount > 0) shelves += static_cast<std::size_t>(s.shelfCount);
        if (shelves > kMaxShelvesPerCabinet)
            return limitError("The sections of " + cab + " hold more than " + std::to_string(kMaxShelvesPerCabinet) +
                                  " adjustable shelves together (limit " + std::to_string(kMaxShelvesPerCabinet) +
                                  " per cabinet; reached at section '" + pathLabel(n.path) + "').",
                              owner);
        if (s.children.size() > kMaxSectionChildren)
            return limitError(label + " is split into " + std::to_string(s.children.size()) +
                                  " child sections; the limit is " + std::to_string(kMaxSectionChildren) + ".",
                              owner);
        if (s.front && s.front->kind == FrontKind::DrawerStack) {
            const auto& heights = s.front->drawerHeights;
            if (heights.size() > kMaxDrawerHeights)
                return limitError(label + " has a drawer stack of " + std::to_string(heights.size()) +
                                      " drawer fronts; the limit is " + std::to_string(kMaxDrawerHeights) + ".",
                                  owner);
            for (std::size_t i = 0; i < heights.size(); ++i)
                if (auto r = checkSize(heights[i], label + ", drawer front " + std::to_string(i + 1), owner); !r) return r;
        }
        for (std::size_t i = s.children.size(); i-- > 0;)
            stack.push_back({&s.children[i], childPath(n.path, i), n.depth + 1});
    }
    const std::size_t parts = estimatePartCount(root);
    if (parts > kMaxPartsPerAssembly)
        return limitError("The interior of " + cab + " would generate up to " + std::to_string(parts) +
                              " parts; the limit is " + std::to_string(kMaxPartsPerAssembly) + " parts per assembly.",
                          owner);
    return {};
}

core::Result<void> checkDefinitionLimits(const CabinetDefinition& definition) {
    const std::string owner = definition.id.str();
    if (auto r = checkSectionLimits(definition.interior, owner); !r) return r;
    // Inputs first (in the standard order), then derived parameters: a too large input is
    // reported under its own name rather than under a formula that uses it.
    std::vector<std::string> names;
    for (const bool derived : {false, true})
        for (const auto& name : definition.parameters.names())
            if (isStandardParameter(name) && isDerivedParameter(name) == derived) names.push_back(name);
    for (const auto& name : names) {
        const auto v = definition.parameters.value(name);
        if (!v) continue; // reported by generate()
        if (!std::isfinite(v.value()) || std::fabs(v.value()) > limits::kMaxDimension)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Parameter '" + name + "' of cabinet '" + owner + "' is " +
                                       (std::isfinite(v.value()) ? num(v.value()) + " mm" : std::string("not finite")) +
                                       "; lengths are limited to " + num(limits::kMaxDimension) + " mm.",
                                   owner);
    }
    return {};
}

} // namespace fos::furniture
