// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/appliance_text.hpp"

#include "fos/core/units.hpp"

#include <QCoreApplication>

namespace fos::app {
namespace {
// Context "fos::app": lupdate files these free tr() calls under the enclosing namespace.
QString tr(const char* text) { return QCoreApplication::translate("fos::app", text); }
} // namespace

bool isApplianceGap(const furniture::CabinetDefinition& cabinet) noexcept { return cabinet.applianceGap.has_value(); }

QString applianceGapDescription(const furniture::CabinetDefinition& cabinet) {
    if (!cabinet.applianceGap) return {};
    QString width = QStringLiteral("?");
    if (cabinet.parameters.find(furniture::appliance_param::kGapWidth)) {
        const auto v = cabinet.parameters.value(furniture::appliance_param::kGapWidth);
        if (v) width = QString::fromStdString(core::formatLength(v.value()));
    }
    switch (cabinet.applianceGap->panels) {
    case furniture::GapPanels::None: return tr("Appliance gap %1 mm, no end panel").arg(width);
    case furniture::GapPanels::Left: return tr("Appliance gap %1 mm, end panel left").arg(width);
    case furniture::GapPanels::Right: return tr("Appliance gap %1 mm, end panel right").arg(width);
    case furniture::GapPanels::Both: return tr("Appliance gap %1 mm, end panels left and right").arg(width);
    }
    return {};
}

QString nicheVentTitle(furniture::NicheVent vent) {
    switch (vent) {
    case furniture::NicheVent::None: return tr("No ventilation opening");
    case furniture::NicheVent::ShelfSlot: return tr("Slot in the shelf below");
    case furniture::NicheVent::BackCutout: return tr("Cut-out in the back panel");
    }
    return {};
}

} // namespace fos::app
