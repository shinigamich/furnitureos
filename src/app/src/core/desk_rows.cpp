// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/desk_rows.hpp"

#include "fos/core/units.hpp"
#include "fos/furniture/edit.hpp"

#include <QCoreApplication>
#include <QStringList>

namespace fos::app {
namespace {
// Context "fos::app": lupdate files these free tr() calls under the enclosing namespace.
QString tr(const char* text) { return QCoreApplication::translate("fos::app", text); }

// The command label key, extracted for translation like the keys in command_text.cpp.
[[maybe_unused]] const char* const kDeskKey = QT_TRANSLATE_NOOP("fos::app::CommandText", "Change desk of {0}");
} // namespace

bool isDesk(const furniture::CabinetDefinition& cabinet) noexcept { return cabinet.desk.has_value(); }

DeskInput deskInput(const furniture::DeskSpec& spec) {
    DeskInput in;
    in.support = spec.support;
    in.frontCornerRadius = spec.frontCornerRadius;
    in.modestyPanel = spec.modestyPanel;
    in.modestySetback = spec.modestySetback;
    in.modestyHeight = spec.modestyHeight;
    in.cableSlot = spec.cableCutout.has_value();
    if (spec.cableCutout) in.cable = *spec.cableCutout;
    in.pedestal = spec.pedestal.has_value();
    if (spec.pedestal) in.pedestalSpec = *spec.pedestal;
    return in;
}

furniture::DeskSpec deskSpec(const DeskInput& input, furniture::DeskSpec base) {
    base.support = input.support;
    base.frontCornerRadius = input.frontCornerRadius;
    const bool panels = input.support == furniture::DeskSupport::Panels;
    base.modestyPanel = panels && input.modestyPanel;
    base.modestySetback = input.modestySetback;
    base.modestyHeight = input.modestyHeight;
    base.cableCutout = input.cableSlot ? std::optional(input.cable) : std::nullopt;
    base.pedestal = panels && input.pedestal ? std::optional(input.pedestalSpec) : std::nullopt;
    return base;
}

project::Command deskCommand(const std::string& itemId, const DeskInput& input) {
    project::Command c;
    c.label = {"Change desk of {0}", {itemId}};
    c.apply = [itemId, input](project::Project& p) -> core::Result<project::Affected> {
        project::Item* item = p.findItem(itemId);
        if (!item)
            return core::makeError(core::ErrorCode::NotFound, "There is no item \"" + itemId + "\" in the project.", itemId);
        furniture::CabinetDefinition* cab = item->cabinet();
        if (!cab || !cab->desk)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Item \"" + itemId + "\" is not a desk or table; it has no desk settings.", itemId);
        furniture::CabinetDefinition probe = *cab;
        probe.desk = deskSpec(input, *cab->desk);
        project::Affected a;
        a.items = {itemId};
        // The generic leg must be in the library before the generator judges a desk on legs.
        for (auto& hw : furniture::requiredGenericHardware(probe))
            if (!p.libraries.findHardware(hw.id)) {
                p.libraries.hardware.push_back(std::move(hw));
                a.libraries = true;
            }
        const furniture::DeskSpec wanted = *probe.desk;
        auto edited = furniture::applyEdit(*cab, p.libraries, [&wanted](furniture::CabinetDefinition& d) -> core::Result<void> {
            d.desk = wanted;
            return {};
        });
        if (!edited) return edited.error(); // the project copy (and its library) is discarded
        return a;
    };
    return c;
}

QString deskDescription(const furniture::CabinetDefinition& cabinet) {
    if (!cabinet.desk) return {};
    const furniture::DeskSpec& d = *cabinet.desk;
    QStringList parts;
    if (d.support == furniture::DeskSupport::Legs) {
        parts << tr("Table on 4 legs");
    } else {
        parts << tr("Desk on end panels");
        if (d.modestyPanel) parts << tr("modesty panel");
    }
    if (d.frontCornerRadius > 0)
        parts << tr("corner radius %1 mm").arg(QString::fromStdString(core::formatLength(d.frontCornerRadius)));
    if (d.cableCutout) parts << tr("cable slot");
    if (d.pedestal)
        parts << (d.pedestal->side == furniture::DeskPedestalSide::Left
                      ? tr("%1-drawer pedestal left").arg(d.pedestal->drawers)
                      : tr("%1-drawer pedestal right").arg(d.pedestal->drawers));
    return parts.join(QStringLiteral(", "));
}

} // namespace fos::app
