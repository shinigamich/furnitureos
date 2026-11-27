// SPDX-License-Identifier: AGPL-3.0-only
// Mirror, array, align and distribute of several items (edit_tools.hpp).
#include "edit_tools_detail.hpp"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace fos::app {
namespace {

[[maybe_unused]] const char* const kLayoutKeys[] = {
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Mirror {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Mirror {0} items"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Array {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Array {0} items"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Align {0} items"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Distribute {0} items"),
};

using detail::axisValue;
using detail::axisVector;
using detail::checkItemIds;
using detail::itemsLabel;

core::Error invalid(std::string message) { return core::makeError(core::ErrorCode::InvalidArgument, std::move(message)); }

/// Index (0 = X, 1 = Y, 2 = Z) of the frame axis of `t` parallel to world axis `a`, or -1 when
/// no frame axis is (within 1e-9) parallel to it.
int alignedFrameAxis(const geometry::Transform& t, Axis a) {
    const geometry::Vec3 axes[3] = {t.xAxis, t.yAxis, t.zAxis};
    for (int k = 0; k < 3; ++k)
        if (std::abs(std::abs(axisValue(axes[k], a)) - 1.0) < 1e-9) return k;
    return -1;
}

/// Panel definition reflected across a plane whose normal is the assembly axis `frameAxis`
/// (a rectangular panel is its own mirror image; only the edges facing along the normal swap).
furniture::PanelDefinition reflectedPanel(furniture::PanelDefinition panel, int frameAxis) {
    using O = furniture::PanelOrientation;
    // Which panel dimension runs along each assembly axis: 'L' length, 'W' width, 'T' thickness.
    const char* dims = panel.orientation == O::Horizontal ? "LWT" : panel.orientation == O::VerticalSide ? "TWL" : "LTW";
    auto& e = panel.edges;
    const auto idx = [](domain::EdgeSide s) { return static_cast<std::size_t>(s); };
    if (dims[frameAxis] == 'L') std::swap(e[idx(domain::EdgeSide::W1)], e[idx(domain::EdgeSide::W2)]);
    if (dims[frameAxis] == 'W') std::swap(e[idx(domain::EdgeSide::L1)], e[idx(domain::EdgeSide::L2)]);
    return panel;
}

double edgeValue(const geometry::Aabb& box, AlignEdge edge) {
    switch (edge) {
    case AlignEdge::Left: return box.min.x;
    case AlignEdge::Right: return box.max.x;
    case AlignEdge::Front: return box.min.y;
    case AlignEdge::Back: return box.max.y;
    case AlignEdge::Bottom: return box.min.z;
    case AlignEdge::Top: return box.max.z;
    }
    return 0;
}

Axis edgeAxis(AlignEdge edge) {
    switch (edge) {
    case AlignEdge::Left:
    case AlignEdge::Right: return Axis::X;
    case AlignEdge::Front:
    case AlignEdge::Back: return Axis::Y;
    case AlignEdge::Bottom:
    case AlignEdge::Top: return Axis::Z;
    }
    return Axis::X;
}

} // namespace

core::Result<CopyPlan> mirrorItemsCommand(const project::Project& project, const project::Resolved& resolved,
                                          const std::vector<std::string>& itemIds, Axis normal) {
    if (auto r = checkItemIds(project, itemIds); !r) return r.error();
    const auto all = itemsBounds(resolved, itemIds);
    if (!all) return all.error();
    const double c = axisValue(all.value().center(), normal);
    CopyPlan plan;
    std::vector<project::Command> list;
    std::vector<std::string> ids, names;
    for (const auto& id : itemIds) {
        const project::Item& item = *project.findItem(id);
        const geometry::Aabb box = *itemBounds(resolved, id);
        // Reflected box: new min = 2c - max, so the shift is (2c - max) - min along the normal.
        const double shift = 2.0 * c - axisValue(box.max, normal) - axisValue(box.min, normal);
        const auto placement = geometry::Transform::translation(axisVector(normal, shift)) * item.placement();
        const auto fresh = freshCopyNames(project, id, 1, &ids, &names).front();
        list.push_back(project::commands::duplicateItem(id, fresh.first, fresh.second, placement));
        plan.newIds.push_back(fresh.first);
        const project::PanelItem* panel = item.panel();
        const int frameAxis = panel ? alignedFrameAxis(panel->placement, normal) : -1;
        if (panel && !panel->panel.contour && frameAxis >= 0) {
            auto def = reflectedPanel(panel->panel, frameAxis);
            def.id = domain::PartId(fresh.first + "/panel");
            def.name = fresh.second;
            if (def.edges != panel->panel.edges) list.push_back(project::commands::setPanel(fresh.first, std::move(def)));
        } else {
            plan.notReflected.push_back(fresh.first);
        }
    }
    plan.command = groupCommands(itemsLabel("Mirror", itemIds), std::move(list));
    return plan;
}

core::Result<CopyPlan> arrayItemsCommand(const project::Project& project, const std::vector<std::string>& itemIds,
                                         int count, geometry::Vec3 step) {
    if (auto r = checkItemIds(project, itemIds); !r) return r.error();
    if (count < 1 || count > 500)
        return invalid("The array needs 1 to 500 copies; " + std::to_string(count) + " was given.");
    if (!std::isfinite(step.x) || !std::isfinite(step.y) || !std::isfinite(step.z))
        return invalid("The array step must be finite numbers of mm.");
    if (step.x == 0 && step.y == 0 && step.z == 0)
        return invalid("The array step is 0 mm: every copy would lie on the original.");
    CopyPlan plan;
    std::vector<project::Command> list;
    std::vector<std::string> ids, names;
    for (const auto& id : itemIds) {
        const auto fresh = freshCopyNames(project, id, count, &ids, &names);
        const auto& placement = project.findItem(id)->placement();
        for (int k = 1; k <= count; ++k) {
            const auto& [newId, newName] = fresh[static_cast<std::size_t>(k - 1)];
            list.push_back(project::commands::duplicateItem(
                id, newId, newName, geometry::Transform::translation(step * static_cast<double>(k)) * placement));
            plan.newIds.push_back(newId);
        }
    }
    plan.command = groupCommands(itemsLabel("Array", itemIds), std::move(list));
    return plan;
}

core::Result<project::Command> alignItemsCommand(const project::Project& project, const project::Resolved& resolved,
                                                 const std::vector<std::string>& itemIds, AlignEdge edge) {
    if (auto r = checkItemIds(project, itemIds); !r) return r.error();
    if (itemIds.size() < 2) return invalid("Align needs at least two items: the first selected one is the reference.");
    if (auto all = itemsBounds(resolved, itemIds); !all) return all.error();
    const Axis axis = edgeAxis(edge);
    const double target = edgeValue(*itemBounds(resolved, itemIds.front()), edge);
    std::vector<project::Command> list;
    for (std::size_t i = 1; i < itemIds.size(); ++i) {
        const double d = target - edgeValue(*itemBounds(resolved, itemIds[i]), edge);
        if (d == 0) continue;
        const auto shift = geometry::Transform::translation(axisVector(axis, d));
        list.push_back(project::commands::setPlacement(itemIds[i], shift * project.findItem(itemIds[i])->placement()));
    }
    return groupCommands({"Align {0} items", {std::to_string(itemIds.size())}}, std::move(list));
}

core::Result<project::Command> distributeItemsCommand(const project::Project& project,
                                                      const project::Resolved& resolved,
                                                      const std::vector<std::string>& itemIds, Axis axis) {
    if (auto r = checkItemIds(project, itemIds); !r) return r.error();
    if (itemIds.size() < 3)
        return invalid("Distribute needs at least three items (the outer two stay where they are).");
    if (auto all = itemsBounds(resolved, itemIds); !all) return all.error();
    std::vector<geometry::Aabb> boxes;
    for (const auto& id : itemIds) boxes.push_back(*itemBounds(resolved, id));
    std::vector<std::size_t> order(itemIds.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return axisValue(boxes[a].min, axis) < axisValue(boxes[b].min, axis);
    });
    const auto size = [&](std::size_t i) { return axisValue(boxes[i].max, axis) - axisValue(boxes[i].min, axis); };
    double sizes = 0;
    for (std::size_t i : order) sizes += size(i);
    const double start = axisValue(boxes[order.front()].min, axis);
    const double end = axisValue(boxes[order.back()].max, axis);
    const double gap = (end - start - sizes) / static_cast<double>(order.size() - 1);
    std::vector<project::Command> list;
    double cursor = start + size(order.front()) + gap;
    for (std::size_t k = 1; k + 1 < order.size(); ++k) {
        const std::size_t i = order[k];
        const double d = cursor - axisValue(boxes[i].min, axis);
        cursor += size(i) + gap;
        if (std::abs(d) < 1e-9) continue;
        const auto shift = geometry::Transform::translation(axisVector(axis, d));
        list.push_back(project::commands::setPlacement(itemIds[i], shift * project.findItem(itemIds[i])->placement()));
    }
    return groupCommands({"Distribute {0} items", {std::to_string(itemIds.size())}}, std::move(list));
}

} // namespace fos::app
