// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// 3D view tools of the main window (phase 15): the section-plane toolbar and the status-bar
/// text of the measurement tool. Both only drive the viewport's display state; neither
/// changes the project.
#include <QString>
#include <QToolBar>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSlider;

namespace fos::viewport {
class ViewportWidget;
}

namespace fos::app::gui {

/// Toolbar "Section plane": axis (X/Y/Z), position slider + spin box (mm, project frame, range =
/// model bounds along the axis), "Flip" (keep the other side) and "Off". While the toolbar is
/// enabled (setSectionEnabled(true)) every change is pushed to the viewport at once.
class SectionToolBar : public QToolBar {
    Q_OBJECT
public:
    SectionToolBar(viewport::ViewportWidget* viewport, QWidget* parent = nullptr);

    /// Turns the section on (plane at the middle of the model on first use) or off.
    void setSectionEnabled(bool on);
    bool sectionEnabled() const noexcept { return enabled_; }
    /// Re-reads the model bounds (call after a model change); keeps the position when possible.
    void updateRange();

    QComboBox* axisBox() const noexcept { return axis_; }
    QDoubleSpinBox* positionBox() const noexcept { return position_; }
    QCheckBox* flipBox() const noexcept { return flip_; }

signals:
    /// "Off" was pressed (the owner unchecks its toggle action).
    void offRequested();

private:
    void push();
    viewport::ViewportWidget* viewport_;
    QComboBox* axis_ = nullptr;
    QSlider* slider_ = nullptr;
    QDoubleSpinBox* position_ = nullptr;
    QCheckBox* flip_ = nullptr;
    bool enabled_ = false;
    bool positioned_ = false;
};

/// Status-bar text of the measurement tool for the viewport's current state (empty when the
/// viewport is not in measure mode), e.g. "Distance 764.0 mm (ΔX 764.0, ΔY 0.0, ΔZ 0.0 mm)".
QString measureStatusText(const viewport::ViewportWidget& viewport);

} // namespace fos::app::gui
