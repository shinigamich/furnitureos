// SPDX-License-Identifier: AGPL-3.0-only
/// Interior: recursive section tree -> partitions, fixed shelves, adjustable shelves, fronts.
#include "gen_context.hpp"

namespace fos::furniture::detail {
namespace {

core::Error invalid(const std::string& message, const Gen& g) {
    return core::makeError(core::ErrorCode::InvalidArgument, message, g.def.id.str());
}

/// Structural checks that do not depend on dimensions.
core::Result<void> checkTree(const Gen& g, const Section& s, const std::string& path, bool coveredByFront) {
    const std::string label = "Section '" + sectionLabel(path) + "' of cabinet '" + g.def.id.str() + "'";
    if (s.shelfCount < 0)
        return invalid(label + " has shelf count " + std::to_string(s.shelfCount) + "; it must be 0 or more.", g);
    if (s.front) {
        if (coveredByFront)
            return invalid(label + " has a front, but an enclosing section already has one covering it. Remove one of the "
                                   "two fronts.", g);
        if (s.front->overlay == OverlayKind::Inset && s.split != SplitDirection::None)
            return invalid(label + " is split by dividers and cannot take an inset front (it would collide with the "
                                   "dividers). Put inset fronts on the leaf sections or use a Full / Half overlay.", g);
        if (s.front->kind == FrontKind::DrawerStack && s.front->drawerHeights.empty())
            return invalid(label + " has a drawer stack without any drawer front; add at least one height entry.", g);
    }
    if (s.split == SplitDirection::None) {
        if (!s.children.empty())
            return invalid(label + " is a leaf (split 'None') but has " + std::to_string(s.children.size()) +
                               " child sections. Set the split direction or remove the children.", g);
        return {};
    }
    if (s.children.size() < 2)
        return invalid(label + " is split but has " + std::to_string(s.children.size()) +
                           " child section(s); a split needs at least 2.", g);
    if (s.shelfCount != 0)
        return invalid(label + " is split and cannot hold adjustable shelves itself (shelf count " +
                           std::to_string(s.shelfCount) + "); put the shelves into its child sections.", g);
    for (std::size_t i = 0; i < s.children.size(); ++i)
        if (auto r = checkTree(g, s.children[i], joinSlot({path, std::to_string(i + 1)}), coveredByFront || s.front); !r)
            return r;
    return {};
}

void corner(Gen& g, std::size_t facePart, std::size_t edgePart, const std::string& edgeSlot) {
    const JointFastener f = g.def.construction.dividerFastener;
    if (f == JointFastener::None) return;
    Job j;
    j.kind = JobKind::Corner;
    j.instance = "hw.joint." + edgeSlot + "." + g.assembly.parts[facePart].custom.at("furniture.slot");
    j.a = facePart;
    j.b = edgePart;
    j.fastener = f;
    g.jobs.push_back(std::move(j));
}

core::Result<void> buildShelves(Gen& g, const Section& s, const std::string& path, const Box& box, const Bounds& b) {
    namespace p = param;
    if (s.shelfCount == 0) return {};
    const double T = g.v(p::kThickness), clearance = g.v(p::kShelfClearance), setback = g.v(p::kShelfSetback);
    const int n = s.shelfCount;
    const double height = box.z1 - box.z0, length = box.x1 - box.x0 - 2 * clearance;
    // n shelves of thickness T leave n + 1 equal clear spaces.
    const double space = (height - n * T) / (n + 1);
    if (space <= core::kLengthEps)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               "Section '" + sectionLabel(path) + "' of cabinet '" + g.def.id.str() + "' is " + num(height) +
                                   " mm high; " + std::to_string(n) + " shelves of " + num(T) + " mm need more than " +
                                   num(n * T) + " mm. Reduce the shelf count.",
                               g.def.id.str());
    if (length <= core::kLengthEps)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               "Section '" + sectionLabel(path) + "' of cabinet '" + g.def.id.str() + "' is " +
                                   num(box.x1 - box.x0) + " mm wide; with a shelf clearance of " + num(clearance) +
                                   " mm on each side no shelf length remains.",
                               g.def.id.str());
    for (int i = 1; i <= n; ++i) {
        const std::string slot = joinSlot({"shelf", path, std::to_string(i)});
        const double z = box.z0 + i * space + (i - 1) * T;
        const std::size_t shelf =
            g.addPart(slot, "Shelf", domain::PartRole::Shelf, MaterialRole::Carcass, length, g.v(p::kShelfDepth), T,
                      Transform::translation({box.x0 + clearance, setback, z}),
                      Edges{EdgeUse::Visible, EdgeUse::Raw, EdgeUse::Raw, EdgeUse::Raw});
        if (!g.def.construction.shelfSupports) continue;
        Job j;
        j.kind = JobKind::ShelfPins;
        j.instance = "hw.pins." + slot;
        j.a = shelf;
        j.b = b.left;
        j.c = b.right;
        j.sharedPanel = !b.leftOuter || !b.rightOuter;
        g.jobs.push_back(std::move(j));
    }
    return {};
}

core::Result<void> buildSection(Gen& g, const Section& s, const std::string& path, const Box& box, const Bounds& b) {
    namespace p = param;
    const double T = g.v(p::kThickness), depth = g.v(p::kInteriorDepth);
    if (s.front)
        if (auto r = buildFront(g, *s.front, path, box, b); !r) return r;
    if (s.split == SplitDirection::None) return buildShelves(g, s, path, box, b);

    const bool vertical = s.split == SplitDirection::Vertical;
    const std::size_t n = s.children.size();
    const double extent = vertical ? box.x1 - box.x0 : box.z1 - box.z0;
    const double available = extent - static_cast<double>(n - 1) * T;
    std::vector<SectionSize> sizes;
    for (const auto& c : s.children) sizes.push_back(c.size);
    const std::string what = "Section '" + sectionLabel(path) + "' of cabinet '" + g.def.id.str() + "' (" +
                             (vertical ? "width " : "height ") + num(extent) + " mm, " + std::to_string(n - 1) +
                             (vertical ? " partition(s) of " : " fixed shelf/shelves of ") + num(T) + " mm)";
    if (available <= core::kLengthEps)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               what + ": the dividers alone need " + num(static_cast<double>(n - 1) * T) +
                                   " mm; nothing is left for the child sections.",
                               g.def.id.str());
    auto dist = distribute(g, available, sizes, what);
    if (!dist) return core::makeError(dist.error().code, dist.error().message, g.def.id.str());

    // Create all dividers first so that every child knows both of its neighbours.
    std::vector<std::size_t> dividers;
    double pos = vertical ? box.x0 : box.z0;
    std::vector<double> starts;
    for (std::size_t i = 0; i < n; ++i) {
        starts.push_back(pos);
        pos += dist.value()[i];
        if (i + 1 == n) break;
        const std::string k = std::to_string(i + 1);
        if (vertical) {
            const std::string slot = joinSlot({"partition", path, k});
            const std::size_t part =
                g.addPart(slot, "Partition", domain::PartRole::Partition, MaterialRole::Carcass, box.z1 - box.z0, depth, T,
                          Transform::fromAxes(kUp, kBack, {pos + T, 0, box.z0}),
                          Edges{EdgeUse::Visible, EdgeUse::Raw, EdgeUse::Raw, EdgeUse::Raw});
            for (const std::size_t f : b.bottom) corner(g, f, part, slot);
            for (const std::size_t f : b.top) corner(g, f, part, slot);
            dividers.push_back(part);
        } else {
            const std::string slot = joinSlot({"fixed_shelf", path, k});
            const std::size_t part =
                g.addPart(slot, "Fixed shelf", domain::PartRole::Shelf, MaterialRole::Carcass, box.x1 - box.x0, depth, T,
                          Transform::translation({box.x0, 0, pos}),
                          Edges{EdgeUse::Visible, EdgeUse::Raw, EdgeUse::Raw, EdgeUse::Raw});
            corner(g, b.left, part, slot);
            corner(g, b.right, part, slot);
            dividers.push_back(part);
        }
        pos += T;
    }
    for (std::size_t i = 0; i < n; ++i) {
        Box cb = box;
        Bounds cbnd = b;
        const double a = starts[i], e = starts[i] + dist.value()[i];
        if (vertical) {
            cb.x0 = a;
            cb.x1 = e;
            if (i > 0) { cbnd.left = dividers[i - 1]; cbnd.leftOuter = false; }
            if (i + 1 < n) { cbnd.right = dividers[i]; cbnd.rightOuter = false; }
        } else {
            cb.z0 = a;
            cb.z1 = e;
            if (i > 0) { cbnd.bottom = {dividers[i - 1]}; cbnd.bottomOuter = false; }
            if (i + 1 < n) { cbnd.top = {dividers[i]}; cbnd.topOuter = false; }
        }
        if (auto r = buildSection(g, s.children[i], joinSlot({path, std::to_string(i + 1)}), cb, cbnd); !r) return r;
    }
    return {};
}

} // namespace

core::Result<void> buildInterior(Gen& g, const Bounds& rootBounds) {
    namespace p = param;
    if (auto r = checkTree(g, g.def.interior, "", false); !r) return r;
    const double T = g.v(p::kThickness);
    const Box box{T, T + g.v(p::kInnerWidth), g.v(p::kInteriorBottom), g.v(p::kInteriorTop)};
    return buildSection(g, g.def.interior, "", box, rootBounds);
}

} // namespace fos::furniture::detail
