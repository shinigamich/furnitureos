// SPDX-License-Identifier: AGPL-3.0-only
// Desk group of the inspector (desks and tables, furniture README "Desks and tables").
#include "form_widgets.hpp"
#include "fos/app/desk_rows.hpp"
#include "fos/app/session.hpp"
#include "inspector_panel.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace fos::app::gui {
namespace {
/// Millimetre field; 0 and negative offsets are accepted here so that the furniture module
/// judges the value and its message is shown.
QDoubleSpinBox* deskSpin(const char* objectName, double value, QWidget* parent, double minimum = 0) {
    auto* s = new QDoubleSpinBox(parent);
    s->setObjectName(QLatin1String(objectName));
    s->setRange(minimum, 5000);
    s->setDecimals(1);
    s->setSuffix(InspectorPanel::tr(" mm"));
    s->setValue(value);
    return s;
}
} // namespace

void InspectorPanel::buildDesk(QVBoxLayout* layout, const std::string& itemId) {
    const furniture::CabinetDefinition& cabinet = *session_.project().findItem(itemId)->cabinet();
    const DeskInput in = deskInput(*cabinet.desk);
    auto* box = new QGroupBox(tr("Desk"), layout->parentWidget());
    box->setObjectName(QStringLiteral("deskGroup"));
    auto* form = new QFormLayout(box);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* support = narrowCombo(box);
    support->setObjectName(QStringLiteral("deskSupport"));
    support->addItem(tr("End panels"), static_cast<int>(furniture::DeskSupport::Panels));
    support->addItem(tr("Four legs"), static_cast<int>(furniture::DeskSupport::Legs));
    selectData(support, static_cast<int>(in.support));
    support->setToolTip(tr("End panels under the top, or four screw-on legs (generic leg, added to the library)."));
    auto* radius = deskSpin("deskCornerRadius", in.frontCornerRadius, box);
    radius->setToolTip(tr("Radius of the two front corners of the top; 0 = sharp corners."));

    auto* modesty = new QCheckBox(tr("Modesty panel"), box);
    modesty->setObjectName(QStringLiteral("deskModesty"));
    modesty->setChecked(in.modestyPanel);
    auto* setback = deskSpin("deskModestySetback", in.modestySetback, box);
    setback->setToolTip(tr("Rear edge of the top to the rear face of the modesty panel."));
    auto* modestyHeight = deskSpin("deskModestyHeight", in.modestyHeight, box);
    modestyHeight->setToolTip(tr("Height of the modesty panel down from the underside of the top."));

    auto* slot = new QCheckBox(tr("Cable slot"), box);
    slot->setObjectName(QStringLiteral("deskCableSlot"));
    slot->setChecked(in.cableSlot);
    auto* slotLength = deskSpin("deskSlotLength", in.cable.length, box);
    auto* slotWidth = deskSpin("deskSlotWidth", in.cable.width, box);
    auto* slotRear = deskSpin("deskSlotRear", in.cable.rearDistance, box);
    slotRear->setToolTip(tr("Rear edge of the top to the slot."));
    auto* slotOffset = deskSpin("deskSlotOffset", in.cable.centerOffset, box, -5000);
    slotOffset->setToolTip(tr("Slot centre right of the desk centre (negative = left)."));

    auto* pedestal = new QCheckBox(tr("Drawer pedestal"), box);
    pedestal->setObjectName(QStringLiteral("deskPedestal"));
    pedestal->setChecked(in.pedestal);
    auto* side = narrowCombo(box);
    side->setObjectName(QStringLiteral("deskPedestalSide"));
    side->addItem(tr("Left"), static_cast<int>(furniture::DeskPedestalSide::Left));
    side->addItem(tr("Right"), static_cast<int>(furniture::DeskPedestalSide::Right));
    selectData(side, static_cast<int>(in.pedestalSpec.side));
    auto* pedestalWidth = deskSpin("deskPedestalWidth", in.pedestalSpec.width, box);
    auto* drawers = new QSpinBox(box);
    drawers->setObjectName(QStringLiteral("deskPedestalDrawers"));
    drawers->setRange(0, 20); // the furniture module judges the count (1 .. 6)
    drawers->setValue(in.pedestalSpec.drawers);

    auto* apply = new QPushButton(tr("Apply desk settings"), box);
    apply->setObjectName(QStringLiteral("applyDesk"));
    auto* error = makeErrorLabel(box);
    error->setObjectName(QStringLiteral("deskError"));

    form->addRow(tr("Support:"), support);
    form->addRow(tr("Front corner radius:"), radius);
    form->addRow(modesty);
    form->addRow(tr("Setback:"), setback);
    form->addRow(tr("Panel height:"), modestyHeight);
    form->addRow(slot);
    form->addRow(tr("Slot length:"), slotLength);
    form->addRow(tr("Slot width:"), slotWidth);
    form->addRow(tr("From rear edge:"), slotRear);
    form->addRow(tr("Centre offset:"), slotOffset);
    form->addRow(pedestal);
    form->addRow(tr("Pedestal side:"), side);
    form->addRow(tr("Pedestal width:"), pedestalWidth);
    form->addRow(tr("Drawers:"), drawers);
    form->addRow(apply);
    form->addRow(error);
    auto* note = new QLabel(tr("Height is from the floor to the top surface. Modesty panel and pedestal need end "
                               "panels. Knee space below 600 mm is a rule-of-thumb warning, not a standard."),
                            box);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    form->addRow(note);

    auto update = [=] {
        const bool panels = valueOf<furniture::DeskSupport>(support) == furniture::DeskSupport::Panels;
        modesty->setEnabled(panels);
        setback->setEnabled(panels && modesty->isChecked());
        modestyHeight->setEnabled(panels && modesty->isChecked());
        for (QWidget* w : std::initializer_list<QWidget*>{slotLength, slotWidth, slotRear, slotOffset})
            w->setEnabled(slot->isChecked());
        pedestal->setEnabled(panels);
        for (QWidget* w : std::initializer_list<QWidget*>{side, pedestalWidth, drawers})
            w->setEnabled(panels && pedestal->isChecked());
    };
    connect(support, qOverload<int>(&QComboBox::currentIndexChanged), box, update);
    for (QCheckBox* c : {modesty, slot, pedestal}) connect(c, &QCheckBox::toggled, box, update);
    update();

    connect(apply, &QPushButton::clicked, this, [=, this] {
        DeskInput next;
        next.support = valueOf<furniture::DeskSupport>(support);
        next.frontCornerRadius = radius->value();
        next.modestyPanel = modesty->isChecked();
        next.modestySetback = setback->value();
        next.modestyHeight = modestyHeight->value();
        next.cableSlot = slot->isChecked();
        next.cable = {slotLength->value(), slotWidth->value(), slotRear->value(), slotOffset->value()};
        next.pedestal = pedestal->isChecked();
        next.pedestalSpec = {valueOf<furniture::DeskPedestalSide>(side), pedestalWidth->value(), drawers->value()};
        run(deskCommand(itemId, next), error);
    });
    layout->addWidget(box);
}

} // namespace fos::app::gui
