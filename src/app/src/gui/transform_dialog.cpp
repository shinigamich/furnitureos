// SPDX-License-Identifier: AGPL-3.0-only
#include "transform_dialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace fos::app::gui {

QDoubleSpinBox* lengthSpin(QWidget* parent, double value) {
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(-100000.0, 100000.0);
    spin->setDecimals(1);
    spin->setSuffix(QObject::tr(" mm"));
    spin->setValue(value);
    return spin;
}

namespace {
QWidget* vectorPage(QDoubleSpinBox* (&spins)[3], geometry::Vec3 value, const QString& note, QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* form = new QFormLayout(page);
    const double v[3] = {value.x, value.y, value.z};
    const char* names[3] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i) {
        spins[i] = lengthSpin(page, v[i]);
        spins[i]->setObjectName(QStringLiteral("%1%2").arg(parent->objectName(), QLatin1String(names[i])));
        form->addRow(QLatin1String(names[i]), spins[i]);
    }
    auto* label = new QLabel(note, page);
    label->setWordWrap(true);
    form->addRow(label);
    return page;
}
} // namespace

TransformDialog::TransformDialog(geometry::Vec3 minCorner, bool panelsOnly, int itemCount, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("transformDialog"));
    setWindowTitle(tr("Move / rotate %n item(s)", nullptr, itemCount));
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("transform"));
    tabs_->addTab(vectorPage(abs_, minCorner,
                             tr("Position of the minimum (left, front, bottom) corner of the selection's box in the "
                                "project frame."),
                             tabs_),
                  tr("Position"));
    tabs_->addTab(vectorPage(rel_, {}, tr("Distance to move the selection by (front = −Y, up = +Z)."), tabs_),
                  tr("Offset"));
    auto* rot = new QWidget(tabs_);
    auto* form = new QFormLayout(rot);
    axis_ = new QComboBox(rot);
    axis_->setObjectName(QStringLiteral("rotationAxis"));
    axis_->addItem(tr("Z (vertical)"), static_cast<int>(Axis::Z));
    if (panelsOnly) {
        axis_->addItem(tr("X (left–right)"), static_cast<int>(Axis::X));
        axis_->addItem(tr("Y (front–back)"), static_cast<int>(Axis::Y));
    }
    angle_ = new QComboBox(rot);
    angle_->setObjectName(QStringLiteral("rotationAngle"));
    angle_->setEditable(true);
    angle_->addItems({QStringLiteral("90"), QStringLiteral("180"), QStringLiteral("270"), QStringLiteral("-90")});
    angle_->lineEdit()->setValidator(new QDoubleValidator(-360.0, 360.0, 3, angle_));
    form->addRow(tr("Axis"), axis_);
    form->addRow(tr("Angle (°)"), angle_);
    auto* note = new QLabel(panelsOnly ? tr("Counter-clockwise looking against the axis, about the centre of the "
                                            "selection's box.")
                                       : tr("Counter-clockwise seen from above, about the centre of the selection's "
                                            "box. Cabinets can only be rotated about the vertical axis."),
                            rot);
    note->setWordWrap(true);
    form->addRow(note);
    tabs_->addTab(rot, tr("Rotate"));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs_);
    layout->addWidget(buttons);
}

TransformDialog::Mode TransformDialog::mode() const { return static_cast<Mode>(tabs_->currentIndex()); }
void TransformDialog::setMode(Mode mode) { tabs_->setCurrentIndex(static_cast<int>(mode)); }
geometry::Vec3 TransformDialog::position() const { return {abs_[0]->value(), abs_[1]->value(), abs_[2]->value()}; }
geometry::Vec3 TransformDialog::offset() const { return {rel_[0]->value(), rel_[1]->value(), rel_[2]->value()}; }
Axis TransformDialog::rotationAxis() const { return static_cast<Axis>(axis_->currentData().toInt()); }
double TransformDialog::angle() const { return QLocale().toDouble(angle_->currentText()); }

ArrayDialog::ArrayDialog(geometry::Vec3 size, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("arrayDialog"));
    setWindowTitle(tr("Linear array"));
    auto* form = new QFormLayout;
    count_ = new QSpinBox(this);
    count_->setObjectName(QStringLiteral("arrayCount"));
    count_->setRange(1, 500);
    count_->setValue(2);
    form->addRow(tr("Copies"), count_);
    const double v[3] = {size.x, 0, 0};
    const char* names[3] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i) {
        step_[i] = lengthSpin(this, v[i]);
        form->addRow(tr("Step %1").arg(QLatin1String(names[i])), step_[i]);
    }
    auto* note = new QLabel(tr("Copy k is moved by k × step from the original (k = 1 … copies)."), this);
    note->setWordWrap(true);
    form->addRow(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

int ArrayDialog::count() const { return count_->value(); }
geometry::Vec3 ArrayDialog::step() const { return {step_[0]->value(), step_[1]->value(), step_[2]->value()}; }

} // namespace fos::app::gui
