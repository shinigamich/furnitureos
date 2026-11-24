// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Plan workspace: 2D top-down arrangement of the project's items (PlanCanvas) with a toolbar
/// (fit, zoom, rotate 90°, export SVG) and an optional rectangular room. Selection is the
/// session selection; every move / rotation is ONE undoable commands::setPlacement
/// (fos/app/plan_view.hpp). The room is not saved: the project file has no place for it, so it
/// lives only as long as this window (stated in the UI).
#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

class PlanCanvas;

class PlanWorkspace : public QWidget {
    Q_OBJECT
public:
    explicit PlanWorkspace(Session& session, QWidget* parent = nullptr);

    /// Rotates the selected item by +90° (one undo step); refusals are shown in the message line.
    void rotateSelected();
    /// Asks for a file name and writes the plan as SVG.
    void exportSvg();
    // Tests.
    PlanCanvas* canvas() const noexcept { return canvas_; }
    QCheckBox* roomBox() const noexcept { return roomOn_; }

protected:
    void showEvent(QShowEvent* event) override;

private:
    void refresh();
    void updateRoom();
    void updateButtons();
    void showMessage(const QString& text);

    Session& session_;
    PlanCanvas* canvas_;
    QPushButton* rotate_;
    QCheckBox* roomOn_;
    QDoubleSpinBox *roomWidth_, *roomDepth_, *roomWall_, *roomX_, *roomY_;
    QLabel* message_;
    bool stale_ = true;
};

} // namespace fos::app::gui
