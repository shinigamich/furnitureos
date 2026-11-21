// SPDX-License-Identifier: AGPL-3.0-only
#include "view_tools.hpp"

#include "fos/viewport/viewport_widget.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>

#include <cmath>

namespace fos::app::gui {
namespace {

viewport::SectionAxis axisOf(int index) {
    return index == 0 ? viewport::SectionAxis::X : index == 1 ? viewport::SectionAxis::Y : viewport::SectionAxis::Z;
}

double along(viewport::SectionAxis axis, geometry::Vec3 p) {
    return axis == viewport::SectionAxis::X ? p.x : axis == viewport::SectionAxis::Y ? p.y : p.z;
}

QString mm(double v) { return QString::number(v, 'f', 1); }

QString snapKindText(viewport::SnapKind k) {
    switch (k) {
    case viewport::SnapKind::Corner: return QCoreApplication::translate("MeasureStatus", "corner");
    case viewport::SnapKind::OutlineVertex: return QCoreApplication::translate("MeasureStatus", "outline vertex");
    case viewport::SnapKind::EdgeMidpoint: return QCoreApplication::translate("MeasureStatus", "edge midpoint");
    case viewport::SnapKind::FaceCenter: return QCoreApplication::translate("MeasureStatus", "face centre");
    case viewport::SnapKind::Surface: break;
    }
    return QCoreApplication::translate("MeasureStatus", "surface point");
}

QString distanceText(const viewport::Measurement& m) {
    const auto d = m.delta();
    return QCoreApplication::translate("MeasureStatus", "Distance %1 mm (ΔX %2, ΔY %3, ΔZ %4 mm)")
        .arg(mm(m.distance()), mm(d.x), mm(d.y), mm(d.z));
}

} // namespace

SectionToolBar::SectionToolBar(viewport::ViewportWidget* viewport, QWidget* parent)
    : QToolBar(tr("Section plane"), parent), viewport_(viewport) {
    setObjectName(QStringLiteral("sectionToolbar"));
    addWidget(new QLabel(tr("Section:") + QLatin1Char(' '), this));
    axis_ = new QComboBox(this);
    axis_->setObjectName(QStringLiteral("sectionAxis"));
    axis_->addItems({tr("X (left–right)"), tr("Y (front–back)"), tr("Z (height)")});
    axis_->setCurrentIndex(1);
    addWidget(axis_);
    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setMinimumWidth(200);
    addWidget(slider_);
    position_ = new QDoubleSpinBox(this);
    position_->setObjectName(QStringLiteral("sectionPosition"));
    position_->setDecimals(1);
    position_->setSuffix(tr(" mm"));
    position_->setToolTip(tr("Position of the plane along the axis (project frame)"));
    addWidget(position_);
    flip_ = new QCheckBox(tr("Flip"), this);
    flip_->setObjectName(QStringLiteral("sectionFlip"));
    flip_->setToolTip(tr("Keep the other side of the plane"));
    addWidget(flip_);
    QAction* off = addAction(tr("Off"));
    off->setToolTip(tr("Remove the section plane"));
    connect(off, &QAction::triggered, this, &SectionToolBar::offRequested);

    connect(axis_, &QComboBox::currentIndexChanged, this, [this] {
        positioned_ = false;
        updateRange();
        push();
    });
    connect(slider_, &QSlider::valueChanged, this, [this](int v) {
        const QSignalBlocker block(position_);
        position_->setValue(v);
        push();
    });
    connect(position_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        const QSignalBlocker block(slider_);
        slider_->setValue(static_cast<int>(std::lround(v)));
        push();
    });
    connect(flip_, &QCheckBox::toggled, this, [this] { push(); });
    updateRange();
}

void SectionToolBar::updateRange() {
    const auto axis = axisOf(axis_->currentIndex());
    const geometry::Aabb b = viewport_->modelBounds();
    double lo = 0, hi = 1000;
    if (!b.empty()) lo = along(axis, b.min), hi = along(axis, b.max);
    const QSignalBlocker b1(slider_), b2(position_);
    position_->setRange(lo, hi);
    slider_->setRange(static_cast<int>(std::floor(lo)), static_cast<int>(std::ceil(hi)));
    if (!positioned_) {
        position_->setValue((lo + hi) / 2);
        positioned_ = !b.empty();
    }
    slider_->setValue(static_cast<int>(std::lround(position_->value())));
    if (enabled_) push();
}

void SectionToolBar::setSectionEnabled(bool on) {
    enabled_ = on;
    setVisible(on);
    if (on)
        updateRange(); // pushes the plane
    else
        viewport_->setSectionPlane(std::nullopt);
}

void SectionToolBar::push() {
    if (!enabled_) return;
    viewport_->setSectionPlane(viewport::SectionPlane{axisOf(axis_->currentIndex()), position_->value(), flip_->isChecked()});
}

QString measureStatusText(const viewport::ViewportWidget& viewport) {
    if (!viewport.measureMode()) return {};
    if (const auto m = viewport.measurement()) return distanceText(*m);
    const auto pts = viewport.measurePoints();
    const auto hover = viewport.hoverSnap();
    QString text = pts.empty()
                       ? QCoreApplication::translate("MeasureStatus", "Measure: click the first point. Esc cancels.")
                       : QCoreApplication::translate("MeasureStatus", "Measure: click the second point. Esc cancels.");
    if (hover) {
        text += QStringLiteral("  —  ") + snapKindText(hover->kind);
        if (pts.size() == 1) text += QStringLiteral("  —  ") + distanceText({pts[0].point, hover->point});
    }
    return text;
}

} // namespace fos::app::gui
