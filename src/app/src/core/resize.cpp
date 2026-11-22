// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/resize.hpp"

#include "fos/app/parameter_rows.hpp"
#include "fos/app/session.hpp"
#include "fos/furniture/panel.hpp"
#include "fos/furniture/parameters.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <cmath>

namespace fos::app {

namespace {

std::optional<double> parameterValue(const project::Item& item, const std::string& name) {
    const auto groups = item.cabinet() ? parameterGroups(*item.cabinet()) : parameterGroups(item.panel()->panel);
    for (const auto& g : groups)
        for (const auto& row : g.rows)
            if (row.name == name) return row.value;
    return std::nullopt;
}

geometry::Vec3 unit(ResizeAxis axis) {
    switch (axis) {
    case ResizeAxis::X: return {1, 0, 0};
    case ResizeAxis::Y: return {0, 1, 0};
    case ResizeAxis::Z: break;
    }
    return {0, 0, 1};
}

/// Box of the panel's generated part in the assembly frame.
geometry::Aabb partBox(const domain::Part& part) {
    return geometry::Aabb::fromSize({part.length, part.width, part.thickness}).transformed(part.placement);
}

} // namespace

std::optional<std::string> resizeParameter(const project::Item& item, ResizeAxis axis) {
    if (item.cabinet()) {
        switch (axis) {
        case ResizeAxis::X: return std::string(furniture::param::kWidth);
        case ResizeAxis::Y: return std::string(furniture::param::kDepth);
        case ResizeAxis::Z: return std::string(furniture::param::kHeight);
        }
        return std::nullopt;
    }
    using furniture::PanelOrientation;
    const std::string length = furniture::panel_param::kLength, width = furniture::panel_param::kWidth;
    switch (item.panel()->panel.orientation) {
    case PanelOrientation::Horizontal:
        if (axis == ResizeAxis::X) return length;
        if (axis == ResizeAxis::Y) return width;
        break;
    case PanelOrientation::VerticalSide:
        if (axis == ResizeAxis::Z) return length;
        if (axis == ResizeAxis::Y) return width;
        break;
    case PanelOrientation::VerticalFront:
        if (axis == ResizeAxis::X) return length;
        if (axis == ResizeAxis::Z) return width;
        break;
    }
    return std::nullopt;
}

std::optional<ResizeTarget> resizeTarget(const Session& session, const std::string& itemId) {
    const project::Item* item = session.project().findItem(itemId);
    const project::Resolved* resolved = session.resolved();
    if (item == nullptr || resolved == nullptr) return std::nullopt;
    const domain::Assembly* assembly = resolved->model.findAssembly(domain::AssemblyId(itemId));
    if (assembly == nullptr) return std::nullopt;

    ResizeTarget t;
    t.itemId = itemId;
    t.placement = item->placement();
    if (item->cabinet()) {
        const auto w = parameterValue(*item, furniture::param::kWidth);
        const auto d = parameterValue(*item, furniture::param::kDepth);
        const auto h = parameterValue(*item, furniture::param::kHeight);
        if (!w || !d || !h) return std::nullopt;
        t.box = geometry::Aabb::fromSize({*w, *d, *h});
    } else {
        if (assembly->parts.size() != 1) return std::nullopt;
        t.box = partBox(assembly->parts.front());
    }
    constexpr std::array<std::pair<ResizeAxis, bool>, 5> order{{{ResizeAxis::X, false},
                                                                 {ResizeAxis::X, true},
                                                                 {ResizeAxis::Y, false},
                                                                 {ResizeAxis::Y, true},
                                                                 {ResizeAxis::Z, false}}};
    for (const auto& [axis, negative] : order) {
        const auto name = resizeParameter(*item, axis);
        if (!name) continue;
        const auto value = parameterValue(*item, *name);
        if (!value) continue;
        t.handles.push_back({axis, negative, *name, *value});
    }
    if (t.handles.empty()) return std::nullopt;
    return t;
}

core::Result<project::Command> resizeCommand(const project::Project& project, const std::string& itemId,
                                             ResizeAxis axis, bool negative, double newValue) {
    const project::Item* item = project.findItem(itemId);
    if (item == nullptr)
        return core::makeError(core::ErrorCode::NotFound, "Item '" + itemId + "' does not exist.", itemId);
    if (!std::isfinite(newValue))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "The new size of '" + item->name() + "' is not a finite number.", itemId);
    if (negative && axis == ResizeAxis::Z)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "'" + item->name() + "' cannot be resized from its bottom face.", itemId);
    const auto name = resizeParameter(*item, axis);
    if (!name)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "'" + item->name() + "' has no size parameter along this axis.", itemId);

    project::Command setValue = project::commands::setParameter(itemId, *name, newValue);
    if (!negative) return setValue;

    const auto oldValue = parameterValue(*item, *name);
    if (!oldValue)
        return core::makeError(core::ErrorCode::NotFound,
                               "'" + item->name() + "' has no parameter '" + *name + "'.", itemId);
    // Keep the opposite face: the assembly origin moves by -delta along the axis (assembly frame).
    const double delta = newValue - *oldValue;
    const geometry::Transform placement =
        item->placement() * geometry::Transform::translation(unit(axis) * -delta);
    project::Command move = project::commands::setPlacement(itemId, placement);

    project::Command combined;
    combined.label = setValue.label;
    combined.apply = [setValue = std::move(setValue), move = std::move(move)](project::Project& p)
        -> core::Result<project::Affected> {
        auto a = setValue.apply(p);
        if (!a) return a.error();
        auto b = move.apply(p);
        if (!b) return b.error();
        project::Affected affected = a.value();
        for (const auto& id : b.value().items)
            if (std::find(affected.items.begin(), affected.items.end(), id) == affected.items.end())
                affected.items.push_back(id);
        return affected;
    };
    return combined;
}

core::Result<void> applyResize(Session& session, const std::string& itemId, ResizeAxis axis, bool negative,
                               double newValue) {
    auto command = resizeCommand(session.project(), itemId, axis, negative, newValue);
    if (!command) return command.error();
    return session.execute(command.value());
}

QString resizeLabel(const std::string& parameter) {
    if (parameter == "width") return QCoreApplication::translate("fos::app::Resize", "W", "width");
    if (parameter == "height") return QCoreApplication::translate("fos::app::Resize", "H", "height");
    if (parameter == "depth") return QCoreApplication::translate("fos::app::Resize", "D", "depth");
    if (parameter == "length") return QCoreApplication::translate("fos::app::Resize", "L", "length");
    return QString::fromStdString(parameter);
}

} // namespace fos::app
