// SPDX-License-Identifier: AGPL-3.0-only
#include "add_panel_dialog.hpp"

#include "fos/app/session.hpp"
#include "fos/core/units.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace fos::app::gui {
namespace {
QString mmText(double v) { return QString::fromStdString(core::formatLength(v)); }

QDoubleSpinBox* sizeSpin(QWidget* parent, double value) {
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(1.0, 100000.0);
    spin->setDecimals(1);
    spin->setSuffix(AddPanelDialog::tr(" mm"));
    spin->setValue(value);
    return spin;
}
} // namespace

AddPanelDialog::AddPanelDialog(const Session& session, std::vector<std::string> selection, QWidget* parent)
    : QDialog(parent), session_(session), selection_(std::move(selection)) {
    setObjectName(QStringLiteral("addPanelDialog"));
    setWindowTitle(tr("Add panel"));
    const auto& libs = session_.project().libraries;

    auto* form = new QFormLayout;
    name_ = new QLineEdit(this);
    name_->setObjectName(QStringLiteral("panelName"));
    name_->setPlaceholderText(tr("Panel"));
    form->addRow(tr("Name"), name_);
    length_ = sizeSpin(this, 600);
    length_->setObjectName(QStringLiteral("panelLength"));
    width_ = sizeSpin(this, 300);
    width_->setObjectName(QStringLiteral("panelWidth"));
    form->addRow(tr("Length"), length_);
    form->addRow(tr("Width"), width_);
    orientation_ = new QComboBox(this);
    orientation_->setObjectName(QStringLiteral("panelOrientation"));
    orientation_->addItem(tr("Horizontal (length along X, width along Y)"),
                          static_cast<int>(furniture::PanelOrientation::Horizontal));
    orientation_->addItem(tr("Vertical, side (thickness along X, length up)"),
                          static_cast<int>(furniture::PanelOrientation::VerticalSide));
    orientation_->addItem(tr("Vertical, front (thickness along Y, width up)"),
                          static_cast<int>(furniture::PanelOrientation::VerticalFront));
    form->addRow(tr("Orientation"), orientation_);
    material_ = new QComboBox(this);
    material_->setObjectName(QStringLiteral("panelMaterial"));
    for (const auto& m : libs.materials)
        material_->addItem(tr("%1 (%2 mm)").arg(QString::fromStdString(m.name), mmText(m.thickness)),
                           QString::fromStdString(m.id.str()));
    form->addRow(tr("Material"), material_);

    auto* edgeBox = new QGroupBox(tr("Edge bands"), this);
    auto* edgeForm = new QFormLayout(edgeBox);
    const QString edgeNames[4] = {tr("L1 (front, along the length)"), tr("L2 (back, along the length)"),
                                  tr("W1 (start, along the width)"), tr("W2 (end, along the width)")};
    for (int i = 0; i < 4; ++i) {
        edges_[i] = new QComboBox(edgeBox);
        edges_[i]->setObjectName(QStringLiteral("panelEdge%1").arg(i));
        edges_[i]->addItem(tr("none"), QString());
        for (const auto& e : libs.edgeBands)
            edges_[i]->addItem(QString::fromStdString(e.name), QString::fromStdString(e.id.str()));
        edgeForm->addRow(edgeNames[i], edges_[i]);
    }

    placement_ = new QComboBox(this);
    placement_->setObjectName(QStringLiteral("panelPlacement"));
    const std::pair<QString, PanelPlacement> places[] = {
        {tr("At the project origin"), PanelPlacement::Origin},
        {tr("At the selection's corner"), PanelPlacement::AtSelection},
        {tr("On top of the selection"), PanelPlacement::OnTop},
        {tr("Below the selection"), PanelPlacement::Below},
        {tr("To the left of the selection"), PanelPlacement::LeftOf},
        {tr("To the right of the selection"), PanelPlacement::RightOf},
        {tr("In front of the selection"), PanelPlacement::InFront},
        {tr("Behind the selection"), PanelPlacement::Behind},
        {tr("Fill the gap between two selected items"), PanelPlacement::FillGap}};
    for (const auto& [text, value] : places) placement_->addItem(text, static_cast<int>(value));
    // Placements that need a selection are disabled without one; Fill gap needs exactly two items.
    if (auto* model = qobject_cast<QStandardItemModel*>(placement_->model()))
        for (int i = 1; i < placement_->count(); ++i) {
            const bool fill = static_cast<PanelPlacement>(placement_->itemData(i).toInt()) == PanelPlacement::FillGap;
            model->item(i)->setEnabled(fill ? selection_.size() == 2 : !selection_.empty());
        }
    if (selection_.size() == 2) setPlacement(PanelPlacement::FillGap);
    else if (!selection_.empty()) setPlacement(PanelPlacement::OnTop);
    form->addRow(tr("Placement"), placement_);
    offset_ = new QDoubleSpinBox(this);
    offset_->setObjectName(QStringLiteral("panelGapOffset"));
    offset_->setRange(0.0, 100000.0);
    offset_->setDecimals(1);
    offset_->setSuffix(tr(" mm"));
    offset_->setToolTip(tr("Fill gap: position of the panel along its thickness inside the gap, from the gap's start "
                           "(for a shelf between two sides: its height above their bottom)."));
    form->addRow(tr("Offset in the gap"), offset_);

    preview_ = new QLabel(this);
    preview_->setObjectName(QStringLiteral("panelPreview"));
    preview_->setWordWrap(true);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    okButton_ = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(edgeBox);
    layout->addWidget(preview_);
    layout->addWidget(buttons);

    for (QDoubleSpinBox* spin : {length_, width_, offset_})
        connect(spin, &QDoubleSpinBox::valueChanged, this, &AddPanelDialog::updatePreview);
    for (QComboBox* box : {orientation_, material_, placement_})
        connect(box, &QComboBox::currentIndexChanged, this, &AddPanelDialog::updatePreview);
    updatePreview();
}

void AddPanelDialog::setPlacement(PanelPlacement placement) {
    placement_->setCurrentIndex(std::max(0, placement_->findData(static_cast<int>(placement))));
}

AddPanelRequest AddPanelDialog::request() const {
    AddPanelRequest r;
    r.name = name_->text().trimmed().toStdString();
    r.length = length_->value();
    r.width = width_->value();
    r.orientation = static_cast<furniture::PanelOrientation>(orientation_->currentData().toInt());
    r.materialId = material_->currentData().toString().toStdString();
    for (int i = 0; i < 4; ++i) {
        const QString band = edges_[i]->currentData().toString();
        if (!band.isEmpty()) r.edges[static_cast<std::size_t>(i)] = band.toStdString();
    }
    r.placement = static_cast<PanelPlacement>(placement_->currentData().toInt());
    if (r.placement != PanelPlacement::Origin) r.referenceIds = selection_;
    r.thicknessOffset = offset_->value();
    return r;
}

void AddPanelDialog::updatePreview() {
    const auto r = request();
    const bool fill = r.placement == PanelPlacement::FillGap;
    length_->setEnabled(!fill);
    width_->setEnabled(!fill);
    offset_->setEnabled(fill);
    const auto plan = planAddPanel(session_.project(), session_.resolved(), r);
    okButton_->setEnabled(plan.ok());
    if (!plan) {
        preview_->setText(QString::fromStdString(plan.error().message));
        preview_->setStyleSheet(QStringLiteral("color: #b00020;"));
        return;
    }
    const auto& p = plan.value();
    preview_->setStyleSheet(QString());
    preview_->setText(tr("Panel %1 × %2 × %3 mm, minimum corner at (%4, %5, %6) mm.")
                          .arg(mmText(p.length), mmText(p.width), mmText(p.thickness), mmText(p.minCorner.x),
                               mmText(p.minCorner.y), mmText(p.minCorner.z)));
}

} // namespace fos::app::gui
