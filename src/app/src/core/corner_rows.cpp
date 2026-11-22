// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/corner_rows.hpp"

#include "fos/core/units.hpp"
#include "fos/furniture/edit.hpp"

#include <QCoreApplication>

#include <algorithm>

namespace fos::app {
namespace {
QString tr(const char* text) { return QCoreApplication::translate("fos::app::Corner", text); }

QString valueOrUnknown(const furniture::CabinetDefinition& cabinet, const char* name) {
    if (!cabinet.parameters.find(name)) return QStringLiteral("?");
    const auto v = cabinet.parameters.value(name);
    return v ? QString::fromStdString(core::formatLength(v.value())) : QStringLiteral("?");
}
} // namespace

bool isCornerCabinet(const furniture::CabinetDefinition& cabinet) noexcept {
    return cabinet.corner.kind != furniture::CornerKind::None;
}

std::vector<std::string> cornerParameterNames(furniture::CornerKind kind) {
    namespace cp = furniture::corner_param;
    switch (kind) {
    case furniture::CornerKind::Blind: return {cp::kBlindWidth};
    case furniture::CornerKind::LShape: return {cp::kWidthB, cp::kDepthB};
    case furniture::CornerKind::None: break;
    }
    return {};
}

std::vector<CornerParameter> cornerParameters(const furniture::CabinetDefinition& cabinet) {
    std::vector<CornerParameter> out;
    const auto groups = parameterGroups(cabinet);
    for (const std::string& name : cornerParameterNames(cabinet.corner.kind)) {
        CornerParameter p{name, std::nullopt};
        for (const auto& g : groups)
            for (const auto& r : g.rows)
                if (r.name == name) p.row = r;
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<ParameterGroup> withoutParameters(std::vector<ParameterGroup> groups, const std::vector<std::string>& names) {
    for (auto& g : groups)
        std::erase_if(g.rows, [&](const ParameterRow& r) {
            return std::find(names.begin(), names.end(), r.name) != names.end();
        });
    std::erase_if(groups, [](const ParameterGroup& g) { return g.rows.empty(); });
    return groups;
}

QString cornerKindTitle(furniture::CornerKind kind) {
    switch (kind) {
    case furniture::CornerKind::Blind: return tr("Blind corner");
    case furniture::CornerKind::LShape: return tr("L-shaped corner");
    case furniture::CornerKind::None: break;
    }
    return tr("None");
}

QString cornerSideTitle(furniture::CornerSide side) {
    return side == furniture::CornerSide::Left ? tr("Left") : tr("Right");
}

QString cornerParameterTitle(const std::string& name) {
    namespace cp = furniture::corner_param;
    if (name == cp::kBlindWidth) return tr("Blind panel width");
    if (name == cp::kWidthB) return tr("Leg B length");
    if (name == cp::kDepthB) return tr("Leg B depth");
    return QString::fromStdString(name);
}

QString cornerDescription(const furniture::CabinetDefinition& cabinet) {
    namespace cp = furniture::corner_param;
    switch (cabinet.corner.kind) {
    case furniture::CornerKind::Blind:
        return cabinet.corner.blindSide == furniture::CornerSide::Left
                   ? tr("Blind corner, blind panel left, %1 mm").arg(valueOrUnknown(cabinet, cp::kBlindWidth))
                   : tr("Blind corner, blind panel right, %1 mm").arg(valueOrUnknown(cabinet, cp::kBlindWidth));
    case furniture::CornerKind::LShape:
        return tr("L-shaped corner, leg B %1 × %2 mm")
            .arg(valueOrUnknown(cabinet, cp::kWidthB), valueOrUnknown(cabinet, cp::kDepthB));
    case furniture::CornerKind::None: break;
    }
    return {};
}

project::Command blindSideCommand(const std::string& itemId, furniture::CornerSide side) {
    project::Command c;
    c.label = {"Set blind side of {0} to {1}", {itemId, furniture::toString(side)}};
    c.apply = [itemId, side](project::Project& p) -> core::Result<project::Affected> {
        project::Item* item = p.findItem(itemId);
        if (!item)
            return core::makeError(core::ErrorCode::NotFound, "There is no item \"" + itemId + "\" in the project.", itemId);
        furniture::CabinetDefinition* cab = item->cabinet();
        if (!cab || cab->corner.kind != furniture::CornerKind::Blind)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Item \"" + itemId + "\" is not a blind corner cabinet; it has no blind side.", itemId);
        auto edited = furniture::applyEdit(*cab, p.libraries, [side](furniture::CabinetDefinition& d) -> core::Result<void> {
            d.corner.blindSide = side;
            if (d.interior.front && d.interior.front->kind == furniture::FrontKind::Door)
                d.interior.front->hingeSide =
                    side == furniture::CornerSide::Left ? furniture::HingeSide::Right : furniture::HingeSide::Left;
            return {};
        });
        if (!edited) return edited.error();
        project::Affected a;
        a.items = {itemId};
        return a;
    };
    return c;
}

} // namespace fos::app
