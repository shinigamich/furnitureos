// SPDX-License-Identifier: AGPL-3.0-only
#include "inner_drawers_box.hpp"

#include "form_widgets.hpp"
#include "fos/app/inner_drawer_rows.hpp"
#include "fos/app/session.hpp"
#include "inspector_panel.hpp"

#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace fos::app::gui {

InnerDrawersBox::InnerDrawersBox(Session& session, std::string itemId, QWidget* parent)
    : QGroupBox(tr("Inner drawers"), parent), session_(session), itemId_(std::move(itemId)) {
    setObjectName(QStringLiteral("innerDrawersBox"));
    auto* form = new QFormLayout(this);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    enabled_ = new QCheckBox(tr("Drawers behind the door"), this);
    enabled_->setObjectName(QStringLiteral("innerDrawers"));
    enabled_->setToolTip(tr("Drawer boxes on slides inside a section that is not split, behind a door, a door pair "
                            "or sliding doors (no adjustable shelves, no clothes rail)."));
    heights_ = new QLineEdit(this);
    heights_->setObjectName(QStringLiteral("innerDrawerHeights"));
    heights_->setToolTip(tr("One compartment per drawer, bottom to top, separated by “;” (e.g. 200; 1*; 1*)."));
    slide_ = narrowCombo(this);
    slide_->setObjectName(QStringLiteral("innerDrawerSlide"));
    bottom_ = choiceBox(drawerBottomChoices(), furniture::DrawerBottomMount::Grooved, this);
    bottom_->setObjectName(QStringLiteral("innerDrawerBottom"));
    joint_ = choiceBox(fastenerChoices(), furniture::JointFastener::Dowel, this);
    joint_->setObjectName(QStringLiteral("innerDrawerJoint"));
    front_ = choiceBox(innerDrawerFrontChoices(), furniture::InnerDrawerFront::Box, this);
    front_->setObjectName(QStringLiteral("innerDrawerFront"));
    handle_ = choiceBox(innerFaciaHandleChoices(), furniture::InnerFaciaHandle::CutOut, this);
    handle_->setObjectName(QStringLiteral("innerDrawerHandle"));
    apply_ = new QPushButton(tr("Apply inner drawers"), this);
    apply_->setObjectName(QStringLiteral("applyInnerDrawers"));
    hint_ = new QLabel(this);
    hint_->setWordWrap(true);
    hint_->setStyleSheet(QStringLiteral("color: palette(mid);"));
    error_ = makeErrorLabel(this);
    error_->setObjectName(QStringLiteral("innerDrawersError"));
    findings_ = makeErrorLabel(this);
    findings_->setObjectName(QStringLiteral("innerDrawersFindings"));
    form->addRow(enabled_);
    form->addRow(tr("Heights:"), heights_);
    form->addRow(tr("Slides:"), slide_);
    form->addRow(tr("Drawer bottom:"), bottom_);
    form->addRow(tr("Box joints:"), joint_);
    form->addRow(tr("Front:"), front_);
    form->addRow(tr("Facia handle:"), handle_);
    form->addRow(apply_);
    form->addRow(hint_);
    form->addRow(error_);
    form->addRow(findings_);

    connect(enabled_, &QCheckBox::toggled, this, [this] { updateEnabled(); });
    connect(front_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateEnabled(); });
    connect(apply_, &QPushButton::clicked, this, &InnerDrawersBox::apply);
    showSection(nullptr);
}

void InnerDrawersBox::showSection(const SectionRow* row) {
    const project::Item* item = session_.project().findItem(itemId_);
    const furniture::CabinetDefinition* cabinet = item ? item->cabinet() : nullptr;
    path_ = row ? row->path : std::string();
    editable_ = row && cabinet && innerDrawersEditable(cabinet->interior, path_);
    const InnerDrawerInput in = innerDrawerInput(row && row->isLeaf() ? row->innerDrawers : std::nullopt);
    enabled_->blockSignals(true);
    enabled_->setChecked(in.enabled);
    enabled_->blockSignals(false);
    heights_->setText(in.heights);
    slide_->clear();
    for (const auto& c : slideChoices(session_.project().libraries, in.slide))
        slide_->addItem(c.missing ? tr("%1 (not in the library)").arg(QString::fromStdString(c.id))
                                  : QString::fromStdString(c.name),
                        QString::fromStdString(c.id));
    selectData(slide_, QString::fromStdString(in.slide));
    selectData(bottom_, static_cast<int>(in.bottom));
    selectData(joint_, static_cast<int>(in.joint));
    selectData(front_, static_cast<int>(in.front));
    selectData(handle_, static_cast<int>(in.handle));
    hint_->setText(row && !editable_
                       ? tr("Inner drawers go into a section that is not split, behind a door, a door pair or "
                            "sliding doors.")
                       : QString());
    hint_->setVisible(!hint_->text().isEmpty());
    showError(error_, {});
    QStringList lines;
    if (const project::Resolved* resolved = session_.resolved(); resolved && row && row->innerDrawers)
        for (const auto& d : innerDrawerFindings(*resolved, itemId_))
            lines << QString::fromStdString(d.message);
    showError(findings_, lines.join(QLatin1Char('\n')));
    updateEnabled();
}

void InnerDrawersBox::updateEnabled() {
    const bool on = editable_ && enabled_->isChecked();
    enabled_->setEnabled(editable_);
    for (QWidget* w : {static_cast<QWidget*>(heights_), static_cast<QWidget*>(slide_),
                       static_cast<QWidget*>(bottom_), static_cast<QWidget*>(joint_), static_cast<QWidget*>(front_)})
        w->setEnabled(on);
    handle_->setEnabled(on && valueOf<furniture::InnerDrawerFront>(front_) == furniture::InnerDrawerFront::Facia);
    apply_->setEnabled(editable_);
}

void InnerDrawersBox::apply() {
    const project::Item* item = session_.project().findItem(itemId_);
    if (!item || !item->cabinet())
        return;
    InnerDrawerInput in;
    in.enabled = enabled_->isChecked();
    in.heights = heights_->text();
    in.slide = slide_->currentData().toString().toStdString();
    in.bottom = valueOf<furniture::DrawerBottomMount>(bottom_);
    in.joint = valueOf<furniture::JointFastener>(joint_);
    in.front = valueOf<furniture::InnerDrawerFront>(front_);
    in.handle = valueOf<furniture::InnerFaciaHandle>(handle_);
    auto command = innerDrawersCommand(*item->cabinet(), itemId_, path_, in);
    if (!command) {
        showError(error_, QString::fromStdString(command.error().message));
        return;
    }
    // On success the inspector rebuilds the editor (projectChanged) and the findings are shown.
    if (auto r = session_.execute(command.value()); !r)
        showError(error_, QString::fromStdString(r.error().message)); // refused: project unchanged
}

} // namespace fos::app::gui
