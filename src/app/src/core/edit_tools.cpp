// SPDX-License-Identifier: AGPL-3.0-only
// Grouped commands, item boxes, move / rotate, delete and duplicate of several items.
#include "edit_tools_detail.hpp"

#include "fos/app/session.hpp"
#include "fos/project/merge.hpp"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace fos::app {
namespace {

// Label keys of the commands built here (translated by commandText(), context CommandText).
[[maybe_unused]] const char* const kEditKeys[] = {
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Move {0} items"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Rotate {0}"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Rotate {0} items"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Delete {0} items"),
    QT_TRANSLATE_NOOP("fos::app::CommandText", "Duplicate {0} items"),
};

core::Error invalid(std::string message) { return core::makeError(core::ErrorCode::InvalidArgument, std::move(message)); }

bool finite(geometry::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

void addUnique(std::vector<std::string>& list, const std::string& id) {
    if (std::find(list.begin(), list.end(), id) == list.end()) list.push_back(id);
}

std::string countText(std::size_t n) { return std::to_string(n); }

} // namespace

namespace detail {
/// Checks a list of item ids: non-empty, no repeats, all existing.
core::Result<void> checkItemIds(const project::Project& project, const std::vector<std::string>& ids) {
    if (ids.empty()) return invalid("Select at least one item.");
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (!project.findItem(ids[i]))
            return core::makeError(core::ErrorCode::NotFound, "Item '" + ids[i] + "' does not exist.", ids[i]);
        if (std::find(ids.begin(), ids.begin() + static_cast<std::ptrdiff_t>(i), ids[i]) !=
            ids.begin() + static_cast<std::ptrdiff_t>(i))
            return invalid("Item '" + ids[i] + "' is listed twice.");
    }
    return {};
}

/// "Verb {0}" with the item id for one item, else "Verb {0} items" with the count.
project::CommandLabel itemsLabel(const std::string& verb, const std::vector<std::string>& ids) {
    if (ids.size() == 1) return {verb + " {0}", {ids[0]}};
    return {verb + " {0} items", {countText(ids.size())}};
}

double axisValue(geometry::Vec3 v, Axis a) { return a == Axis::X ? v.x : a == Axis::Y ? v.y : v.z; }
geometry::Vec3 axisVector(Axis a, double value) {
    return a == Axis::X ? geometry::Vec3{value, 0, 0} : a == Axis::Y ? geometry::Vec3{0, value, 0} : geometry::Vec3{0, 0, value};
}
} // namespace detail

using detail::axisValue;
using detail::axisVector;
using detail::checkItemIds;
using detail::itemsLabel;

project::Command groupCommands(project::CommandLabel label, std::vector<project::Command> commands) {
    project::Command c;
    c.label = std::move(label);
    c.apply = [commands = std::move(commands)](project::Project& p) -> core::Result<project::Affected> {
        project::Affected all;
        std::vector<std::string> touched;
        for (const auto& command : commands) {
            auto r = command.apply(p);
            if (!r) return r.error();
            for (const auto& id : r.value().items) addUnique(touched, id);
            for (const auto& d : r.value().drawings) addUnique(all.drawings, d);
            all.libraries = all.libraries || r.value().libraries;
            all.settings = all.settings || r.value().settings;
        }
        for (const auto& item : p.items)
            if (std::find(touched.begin(), touched.end(), item.id()) != touched.end()) all.items.push_back(item.id());
        for (const auto& id : touched) addUnique(all.items, id); // removed items
        std::sort(all.drawings.begin(), all.drawings.end());
        return all;
    };
    return c;
}

std::optional<geometry::Aabb> itemBounds(const project::Resolved& resolved, const std::string& itemId) {
    const domain::Assembly* a = resolved.model.findAssembly(domain::AssemblyId(itemId));
    if (!a) return std::nullopt;
    const auto box = a->bounds().transformed(a->placement);
    if (box.empty()) return std::nullopt;
    return box;
}

core::Result<geometry::Aabb> itemsBounds(const project::Resolved& resolved, const std::vector<std::string>& itemIds) {
    if (itemIds.empty()) return invalid("Select at least one item.");
    geometry::Aabb all;
    for (const auto& id : itemIds) {
        const auto box = itemBounds(resolved, id);
        if (!box)
            return core::makeError(core::ErrorCode::NotFound,
                                   "Item '" + id + "' has no generated geometry (see Diagnostics), so it cannot be "
                                   "positioned by its box.",
                                   id);
        all.expand(*box);
    }
    return all;
}

geometry::Transform rotationAbout(Axis axis, double degrees, geometry::Vec3 pivot) {
    double turns = degrees / 90.0;
    double c = 0, s = 0;
    if (std::abs(turns - std::round(turns)) < 1e-12) {
        const long q = ((static_cast<long>(std::llround(turns)) % 4) + 4) % 4;
        c = q == 0 ? 1 : q == 2 ? -1 : 0;
        s = q == 1 ? 1 : q == 3 ? -1 : 0;
    } else {
        const double rad = degrees * 3.14159265358979323846 / 180.0;
        c = std::cos(rad);
        s = std::sin(rad);
    }
    geometry::Transform r;
    switch (axis) {
    case Axis::X: r = {{1, 0, 0}, {0, c, s}, {0, -s, c}, {}}; break;
    case Axis::Y: r = {{c, 0, -s}, {0, 1, 0}, {s, 0, c}, {}}; break;
    case Axis::Z: r = {{c, s, 0}, {-s, c, 0}, {0, 0, 1}, {}}; break;
    }
    return geometry::Transform::translation(pivot) * r * geometry::Transform::translation(-pivot);
}

core::Result<project::Command> translateItemsCommand(const project::Project& project,
                                                     const std::vector<std::string>& itemIds, geometry::Vec3 delta) {
    if (auto r = checkItemIds(project, itemIds); !r) return r.error();
    if (!finite(delta)) return invalid("The move distance must be a finite number of mm.");
    std::vector<project::Command> list;
    const auto shift = geometry::Transform::translation(delta);
    for (const auto& id : itemIds)
        list.push_back(project::commands::setPlacement(id, shift * project.findItem(id)->placement()));
    return groupCommands(itemsLabel("Move", itemIds), std::move(list));
}

core::Result<project::Command> moveItemsToCommand(const project::Project& project, const project::Resolved& resolved,
                                                  const std::vector<std::string>& itemIds, geometry::Vec3 minCorner) {
    if (auto r = checkItemIds(project, itemIds); !r) return r.error();
    const auto box = itemsBounds(resolved, itemIds);
    if (!box) return box.error();
    if (!finite(minCorner)) return invalid("The position must be finite numbers of mm.");
    return translateItemsCommand(project, itemIds, minCorner - box.value().min);
}

core::Result<project::Command> rotateItemsCommand(const project::Project& project, const project::Resolved& resolved,
                                                  const std::vector<std::string>& itemIds, Axis axis, double degrees) {
    if (auto r = checkItemIds(project, itemIds); !r) return r.error();
    if (!std::isfinite(degrees)) return invalid("The rotation angle must be a finite number of degrees.");
    if (axis != Axis::Z)
        for (const auto& id : itemIds)
            if (project.findItem(id)->isCabinet())
                return invalid("'" + project.findItem(id)->name() +
                               "' is a cabinet: cabinets can only be rotated about the vertical (Z) axis; "
                               "rotation about X or Y is for free panels.");
    const auto box = itemsBounds(resolved, itemIds);
    if (!box) return box.error();
    const auto rot = rotationAbout(axis, degrees, box.value().center());
    std::vector<project::Command> list;
    for (const auto& id : itemIds)
        list.push_back(project::commands::setPlacement(id, rot * project.findItem(id)->placement()));
    return groupCommands(itemsLabel("Rotate", itemIds), std::move(list));
}

std::vector<std::pair<std::string, std::string>> freshCopyNames(const project::Project& project,
                                                                const std::string& sourceId, int count,
                                                                std::vector<std::string>* usedIds,
                                                                std::vector<std::string>* usedNames) {
    std::vector<std::string> ids, names;
    if (usedIds) ids = *usedIds;
    if (usedNames) names = *usedNames;
    for (const auto& item : project.items) {
        addUnique(ids, item.id());
        addUnique(names, item.name());
    }
    const project::Item* source = project.findItem(sourceId);
    const std::string base = source ? source->name() : sourceId;
    std::vector<std::pair<std::string, std::string>> out;
    for (int k = 0; k < count; ++k) {
        std::string id = project::freeItemId(sourceId, ids);
        ids.push_back(id);
        std::string name = base;
        for (int n = 2; std::find(names.begin(), names.end(), name) != names.end(); ++n)
            name = base + " " + std::to_string(n);
        names.push_back(name);
        out.emplace_back(std::move(id), std::move(name));
    }
    if (usedIds) *usedIds = ids;
    if (usedNames) *usedNames = names;
    return out;
}

core::Result<CopyPlan> duplicateItemsCommand(const project::Project& project, const project::Resolved& resolved,
                                             const std::vector<std::string>& itemIds) {
    if (auto r = checkItemIds(project, itemIds); !r) return r.error();
    const auto box = itemsBounds(resolved, itemIds);
    if (!box) return box.error();
    double right = box.value().max.x;
    for (const auto& a : resolved.model.assemblies) {
        const auto b = a.bounds().transformed(a.placement);
        if (!b.empty()) right = std::max(right, b.max.x);
    }
    const auto shift = geometry::Transform::translation({right - box.value().min.x, 0, 0});
    CopyPlan plan;
    std::vector<project::Command> list;
    std::vector<std::string> ids, names;
    for (const auto& id : itemIds) {
        const auto fresh = freshCopyNames(project, id, 1, &ids, &names).front();
        list.push_back(project::commands::duplicateItem(id, fresh.first, fresh.second,
                                                        shift * project.findItem(id)->placement()));
        plan.newIds.push_back(fresh.first);
    }
    plan.command = itemIds.size() == 1 ? std::move(list.front())
                                       : groupCommands(itemsLabel("Duplicate", itemIds), std::move(list));
    return plan;
}

core::Result<project::Command> removeItemsCommand(const project::Project& project,
                                                  const std::vector<std::string>& itemIds) {
    if (auto r = checkItemIds(project, itemIds); !r) return r.error();
    if (itemIds.size() == 1) return project::commands::removeItem(itemIds.front());
    std::vector<project::Command> list;
    for (const auto& id : itemIds) list.push_back(project::commands::removeItem(id));
    return groupCommands(itemsLabel("Delete", itemIds), std::move(list));
}

core::Result<void> applyItemsCommand(Session& session, const core::Result<project::Command>& command) {
    if (!command) return command.error();
    return session.execute(command.value());
}

core::Result<std::vector<std::string>> applyCopyPlan(Session& session, const core::Result<CopyPlan>& plan) {
    if (!plan) return plan.error();
    if (auto r = session.execute(plan.value().command); !r) return r.error();
    session.selectItems(plan.value().newIds);
    return plan.value().newIds;
}

namespace {
core::Error noModel(const Session& session) {
    return core::makeError(core::ErrorCode::InvalidArgument,
                           "The project could not be resolved: " + session.resolveError());
}
} // namespace

core::Result<void> deleteSelectedItems(Session& session) {
    const auto ids = session.selection().allItems();
    auto r = applyItemsCommand(session, removeItemsCommand(session.project(), ids));
    if (r) session.setSelection({});
    return r;
}

core::Result<std::vector<std::string>> duplicateSelectedItems(Session& session) {
    if (!session.resolved()) return noModel(session);
    return applyCopyPlan(session, duplicateItemsCommand(session.project(), *session.resolved(),
                                                        session.selection().allItems()));
}

core::Result<void> nudgeSelectedItems(Session& session, geometry::Vec3 delta) {
    return applyItemsCommand(session, translateItemsCommand(session.project(), session.selection().allItems(), delta));
}

} // namespace fos::app
