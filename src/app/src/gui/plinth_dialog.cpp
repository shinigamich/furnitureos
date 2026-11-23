// SPDX-License-Identifier: AGPL-3.0-only
#include "plinth_dialog.hpp"

#include "fos/app/session.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace fos::app::gui {
namespace {

QDoubleSpinBox* lengthBox(double min, double max, double value, QWidget* parent) {
    auto* box = new QDoubleSpinBox(parent);
    box->setRange(min, max);
    box->setDecimals(1);
    box->setSuffix(QStringLiteral(" mm"));
    box->setValue(value);
    box->setKeyboardTracking(false);
    return box;
}

} // namespace

PlinthDialog::PlinthDialog(const project::Project& project, const Selection& selection, QWidget* parent)
    : QDialog(parent), project_(project) {
    setWindowTitle(tr("Add plinth"));
    setObjectName(QStringLiteral("plinthDialog"));
    const project::PlinthRequest defaults; // engine defaults
    base_ = defaultPlinthRequest(project, {});

    // --- cabinets
    cabinets_ = new QListWidget(this);
    cabinets_->setObjectName(QStringLiteral("plinthCabinets"));
    for (const auto& row : cabinetRunRows(project, selection)) {
        auto* item = new QListWidgetItem(row.label, cabinets_);
        item->setData(Qt::UserRole, QString::fromStdString(row.id));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(row.checked ? Qt::Checked : Qt::Unchecked);
    }
    auto* cabinetBox = new QGroupBox(tr("Cabinets of the run (first checked = reference)"), this);
    auto* cabinetLayout = new QVBoxLayout(cabinetBox);
    cabinetLayout->addWidget(cabinets_);

    // --- options (ranges = the engine's documented ranges)
    material_ = new QComboBox(this);
    for (const auto& c : plinthMaterialChoices(project)) material_->addItem(c.text, QString::fromStdString(c.id));
    setback_ = lengthBox(0, 300, defaults.setback, this);
    setback_->setToolTip(tr("Plinth face behind the foremost carcass front (50 mm is a rule of thumb)."));
    height_ = lengthBox(0, 300, 0, this);
    height_->setSpecialValueText(tr("From the cabinets"));
    height_->setToolTip(tr("Board height from the floor; minimum = the plinth height of the cabinets on feet."));
    returnLeft_ = new QCheckBox(tr("Left"), this);
    returnLeft_->setChecked(defaults.returnLeft);
    returnRight_ = new QCheckBox(tr("Right"), this);
    returnRight_->setChecked(defaults.returnRight);
    auto* returns = new QHBoxLayout;
    returns->addWidget(returnLeft_);
    returns->addWidget(returnRight_);
    returns->addStretch();
    maxPiece_ = lengthBox(0, 100000, 0, this);
    maxPiece_->setSpecialValueText(tr("Stock length"));

    auto* form = new QFormLayout;
    form->addRow(tr("Material:"), material_);
    form->addRow(tr("Setback:"), setback_);
    form->addRow(tr("Height:"), height_);
    form->addRow(tr("End returns:"), returns);
    form->addRow(tr("Longest piece:"), maxPiece_);
    auto* note = new QLabel(tr("Cabinets must stand on feet; appliance gaps interrupt the plinth. Plinth clips and "
                               "feet fixing are not modelled."),
                            this);
    note->setWordWrap(true);

    // --- preview
    previewText_ = new QPlainTextEdit(this);
    previewText_->setObjectName(QStringLiteral("plinthPreview"));
    previewText_->setReadOnly(true);
    errorLabel_ = new QLabel(this);
    errorLabel_->setObjectName(QStringLiteral("plinthError"));
    errorLabel_->setWordWrap(true);
    errorLabel_->setStyleSheet(QStringLiteral("color: #b00020;"));
    auto* previewBox = new QGroupBox(tr("Preview"), this);
    auto* previewLayout = new QVBoxLayout(previewBox);
    previewLayout->addWidget(previewText_);
    previewLayout->addWidget(errorLabel_);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* left = new QVBoxLayout;
    left->addWidget(cabinetBox);
    left->addLayout(form);
    left->addWidget(note);
    auto* columns = new QHBoxLayout;
    columns->addLayout(left);
    columns->addWidget(previewBox, 1);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(columns);
    layout->addWidget(buttons_);

    // --- live preview
    auto refresh = [this] { updatePreview(); };
    connect(cabinets_, &QListWidget::itemChanged, this, refresh);
    connect(material_, &QComboBox::currentIndexChanged, this, refresh);
    for (QCheckBox* box : {returnLeft_, returnRight_}) connect(box, &QCheckBox::toggled, this, refresh);
    for (QDoubleSpinBox* box : {setback_, height_, maxPiece_}) connect(box, &QDoubleSpinBox::valueChanged, this, refresh);
    resize(860, 520);
    updatePreview();
}

project::PlinthRequest PlinthDialog::request() const {
    project::PlinthRequest r = base_;
    r.itemIds.clear();
    for (int i = 0; i < cabinets_->count(); ++i) {
        const QListWidgetItem* item = cabinets_->item(i);
        if (item->checkState() == Qt::Checked) r.itemIds.push_back(item->data(Qt::UserRole).toString().toStdString());
    }
    r.materialId = material_->currentData().toString().toStdString();
    r.setback = setback_->value();
    r.height = height_->value() > 0 ? std::optional<double>(height_->value()) : std::nullopt;
    r.returnLeft = returnLeft_->isChecked();
    r.returnRight = returnRight_->isChecked();
    r.maxPieceLength = maxPiece_->value() > 0 ? std::optional<double>(maxPiece_->value()) : std::nullopt;
    return r;
}

void PlinthDialog::updatePreview() {
    preview_ = plinthPreview(project_, request());
    previewText_->setPlainText(preview_.text);
    errorLabel_->setText(preview_.error);
    errorLabel_->setVisible(!preview_.ok);
    QPushButton* ok = buttons_->button(QDialogButtonBox::Ok);
    ok->setEnabled(preview_.ok);
    ok->setToolTip(preview_.ok ? QString() : preview_.error);
}

} // namespace fos::app::gui
