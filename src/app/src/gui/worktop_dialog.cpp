// SPDX-License-Identifier: AGPL-3.0-only
#include "worktop_dialog.hpp"

#include "fos/app/session.hpp"

#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fos::app::gui {
namespace {

enum CutoutColumn { ColKind, ColCenter, ColLength, ColDepth, ColRadius, ColSetback, ColCount };

QDoubleSpinBox* lengthBox(double min, double max, double value, QWidget* parent) {
    auto* box = new QDoubleSpinBox(parent);
    box->setRange(min, max);
    box->setDecimals(1);
    box->setSuffix(QCoreApplication::translate("fos::app::gui::WorktopDialog", " mm"));
    box->setValue(value);
    box->setKeyboardTracking(false);
    return box;
}

QDoubleSpinBox* cellBox(QTableWidget* table, int row, int column) {
    return qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, column));
}

} // namespace

WorktopDialog::WorktopDialog(const project::Project& project, const Selection& selection, QWidget* parent)
    : QDialog(parent), project_(project) {
    setWindowTitle(tr("Add worktop"));
    setObjectName(QStringLiteral("worktopDialog"));
    const project::WorktopRequest defaults; // engine defaults
    base_ = defaultWorktopRequest(project, {});

    // --- cabinets
    cabinets_ = new QListWidget(this);
    cabinets_->setObjectName(QStringLiteral("worktopCabinets"));
    for (const auto& row : worktopCabinetRows(project, selection)) {
        auto* item = new QListWidgetItem(row.label, cabinets_);
        item->setData(Qt::UserRole, QString::fromStdString(row.id));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(row.checked ? Qt::Checked : Qt::Unchecked);
    }
    auto* cabinetBox = new QGroupBox(tr("Cabinets of the run (first checked = reference)"), this);
    auto* cabinetLayout = new QVBoxLayout(cabinetBox);
    cabinetLayout->addWidget(cabinets_);

    // --- material and edges
    material_ = new QComboBox(this);
    for (const auto& c : worktopMaterialChoices(project)) material_->addItem(c.text, QString::fromStdString(c.id));
    band_ = new QComboBox(this);
    for (const auto& c : worktopBandChoices(project)) band_->addItem(c.text, QString::fromStdString(c.id));
    bandFront_ = new QCheckBox(tr("Front edge"), this);
    bandFront_->setChecked(defaults.bandFront);
    bandBack_ = new QCheckBox(tr("Back edge"), this);
    bandBack_->setChecked(defaults.bandBack);
    bandLeft_ = new QCheckBox(tr("Left end"), this);
    bandLeft_->setChecked(defaults.bandLeftEnd);
    bandRight_ = new QCheckBox(tr("Right end"), this);
    bandRight_->setChecked(defaults.bandRightEnd);
    auto* bandFlags = new QHBoxLayout;
    for (QCheckBox* box : {bandFront_, bandBack_, bandLeft_, bandRight_}) bandFlags->addWidget(box);
    bandFlags->addStretch();

    // --- sizes (ranges = the engine's documented ranges)
    frontOverhang_ = lengthBox(0, 300, defaults.frontOverhang, this);
    leftOverhang_ = lengthBox(0, 1000, defaults.leftOverhang, this);
    rightOverhang_ = lengthBox(0, 1000, defaults.rightOverhang, this);
    backGap_ = lengthBox(0, 100, defaults.backGap, this);
    maxPiece_ = lengthBox(0, 100000, 0, this);
    maxPiece_->setSpecialValueText(tr("Stock length"));

    auto* form = new QFormLayout;
    form->addRow(tr("Material:"), material_);
    form->addRow(tr("Edge band:"), band_);
    form->addRow(tr("Banded edges:"), bandFlags);
    form->addRow(tr("Front overhang:"), frontOverhang_);
    form->addRow(tr("Left overhang:"), leftOverhang_);
    form->addRow(tr("Right overhang:"), rightOverhang_);
    form->addRow(tr("Gap to the wall:"), backGap_);
    form->addRow(tr("Longest piece:"), maxPiece_);

    // --- cut-outs
    cutouts_ = new QTableWidget(0, ColCount, this);
    cutouts_->setObjectName(QStringLiteral("worktopCutouts"));
    cutouts_->setHorizontalHeaderLabels({tr("Kind"), tr("Centre x"), tr("Length"), tr("Depth"), tr("Corner radius"),
                                         tr("Front setback")});
    cutouts_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    cutouts_->setSelectionBehavior(QAbstractItemView::SelectRows);
    cutouts_->setToolTip(tr("Centre x is measured from the left face of the leftmost cabinet. "
                            "Front setback: distance from the finished front edge; minimum = centred in depth."));
    auto* addCutout = new QPushButton(tr("Add cut-out"), this);
    auto* removeCutout = new QPushButton(tr("Remove cut-out"), this);
    auto* cutoutButtons = new QHBoxLayout;
    cutoutButtons->addWidget(addCutout);
    cutoutButtons->addWidget(removeCutout);
    cutoutButtons->addStretch();
    auto* cutoutBox = new QGroupBox(tr("Cut-outs (sink, hob)"), this);
    auto* cutoutLayout = new QVBoxLayout(cutoutBox);
    cutoutLayout->addWidget(cutouts_);
    cutoutLayout->addLayout(cutoutButtons);
    auto* rulesNote = new QLabel(tr("Minimum distances are rules of thumb (%1 mm to the front and back edges, %2 mm to a "
                                    "free end, %3 mm to a joint, %4 mm between cut-outs), not a standard: always check "
                                    "the installation sheet of the sink or hob.")
                                     .arg(base_.rules.minEdgeDistance)
                                     .arg(base_.rules.minEndDistance)
                                     .arg(base_.rules.minJointDistance)
                                     .arg(base_.rules.minCutoutGap),
                                 this);
    rulesNote->setWordWrap(true);
    cutoutLayout->addWidget(rulesNote);

    // --- preview
    previewText_ = new QPlainTextEdit(this);
    previewText_->setObjectName(QStringLiteral("worktopPreview"));
    previewText_->setReadOnly(true);
    errorLabel_ = new QLabel(this);
    errorLabel_->setObjectName(QStringLiteral("worktopError"));
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
    auto* right = new QVBoxLayout;
    right->addWidget(cutoutBox, 1);
    right->addWidget(previewBox, 1);
    auto* columns = new QHBoxLayout;
    columns->addLayout(left);
    columns->addLayout(right, 1);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(columns);
    layout->addWidget(buttons_);

    // --- live preview
    auto refresh = [this] { updatePreview(); };
    connect(cabinets_, &QListWidget::itemChanged, this, refresh);
    connect(material_, &QComboBox::currentIndexChanged, this, refresh);
    connect(band_, &QComboBox::currentIndexChanged, this, refresh);
    for (QCheckBox* box : {bandFront_, bandBack_, bandLeft_, bandRight_}) connect(box, &QCheckBox::toggled, this, refresh);
    for (QDoubleSpinBox* box : {frontOverhang_, leftOverhang_, rightOverhang_, backGap_, maxPiece_})
        connect(box, &QDoubleSpinBox::valueChanged, this, refresh);
    connect(addCutout, &QPushButton::clicked, this, [this] {
        // A common 780 x 480 sink size as a starting point, at the middle of the worktop.
        project::WorktopCutout c;
        c.length = 780;
        c.depth = 480;
        c.centerX = preview_.ok ? preview_.middleX : c.length;
        addCutoutRow(c);
        updatePreview();
    });
    connect(removeCutout, &QPushButton::clicked, this, [this] {
        const int row = cutouts_->currentRow();
        if (row < 0) return;
        cutouts_->removeRow(row);
        updatePreview();
    });
    resize(1000, 640);
    updatePreview();
}

void WorktopDialog::addCutoutRow(const project::WorktopCutout& cutout) {
    const int row = cutouts_->rowCount();
    cutouts_->insertRow(row);
    auto* kind = new QComboBox(cutouts_);
    for (auto k : worktopCutoutKinds()) {
        kind->addItem(worktopCutoutKindTitle(k), static_cast<int>(k));
        if (k == cutout.kind) kind->setCurrentIndex(kind->count() - 1);
    }
    cutouts_->setCellWidget(row, ColKind, kind);
    connect(kind, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    auto put = [&](int column, double min, double max, double value) {
        auto* box = lengthBox(min, max, value, cutouts_);
        cutouts_->setCellWidget(row, column, box);
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
        return box;
    };
    put(ColCenter, -100000, 100000, cutout.centerX);
    put(ColLength, 1, 10000, cutout.length);
    put(ColDepth, 1, 10000, cutout.depth);
    put(ColRadius, 0, 5000, cutout.cornerRadius);
    put(ColSetback, 0, 10000, cutout.frontSetback.value_or(0))->setSpecialValueText(tr("Centred"));
}

project::WorktopRequest WorktopDialog::request() const {
    project::WorktopRequest r = base_;
    r.itemIds.clear();
    for (int i = 0; i < cabinets_->count(); ++i) {
        const QListWidgetItem* item = cabinets_->item(i);
        if (item->checkState() == Qt::Checked) r.itemIds.push_back(item->data(Qt::UserRole).toString().toStdString());
    }
    r.materialId = material_->currentData().toString().toStdString();
    r.frontBandId = band_->currentData().toString().toStdString();
    r.bandFront = bandFront_->isChecked();
    r.bandBack = bandBack_->isChecked();
    r.bandLeftEnd = bandLeft_->isChecked();
    r.bandRightEnd = bandRight_->isChecked();
    r.frontOverhang = frontOverhang_->value();
    r.leftOverhang = leftOverhang_->value();
    r.rightOverhang = rightOverhang_->value();
    r.backGap = backGap_->value();
    r.maxPieceLength = maxPiece_->value() > 0 ? std::optional<double>(maxPiece_->value()) : std::nullopt;
    r.cutouts.clear();
    for (int row = 0; row < cutouts_->rowCount(); ++row) {
        project::WorktopCutout c;
        if (auto* kind = qobject_cast<QComboBox*>(cutouts_->cellWidget(row, ColKind)))
            c.kind = static_cast<project::WorktopCutoutKind>(kind->currentData().toInt());
        c.centerX = cellBox(cutouts_, row, ColCenter)->value();
        c.length = cellBox(cutouts_, row, ColLength)->value();
        c.depth = cellBox(cutouts_, row, ColDepth)->value();
        c.cornerRadius = cellBox(cutouts_, row, ColRadius)->value();
        const double setback = cellBox(cutouts_, row, ColSetback)->value();
        if (setback > 0) c.frontSetback = setback;
        r.cutouts.push_back(c);
    }
    return r;
}

void WorktopDialog::updatePreview() {
    band_->setEnabled(bandFront_->isChecked());
    preview_ = worktopPreview(project_, request());
    previewText_->setPlainText(preview_.text);
    errorLabel_->setText(preview_.error);
    errorLabel_->setVisible(!preview_.ok);
    QPushButton* ok = buttons_->button(QDialogButtonBox::Ok);
    ok->setEnabled(preview_.ok);
    ok->setToolTip(preview_.ok ? QString() : preview_.error);
}

} // namespace fos::app::gui
