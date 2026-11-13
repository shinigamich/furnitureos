// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Annotation editing in the Drawings workspace: the clickable sheet preview and the side
/// panel that edits the selected dimension (offset, hide, text override), adds user
/// dimensions between two picked part corners, and lists the stored edits with their
/// stale-override / orphan findings. All logic is in fos/app/drawing_edits.hpp; every change
/// is one project command (commands::setElementEdit / setDrawingEdits).
#include "fos/app/drawing_edits.hpp"

#include <QLabel>
#include <QWidget>

#include <optional>
#include <string>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

/// Preview label that reports clicks in pixmap pixel coordinates (the pixmap is centred).
class SheetPreview : public QLabel {
    Q_OBJECT
public:
    explicit SheetPreview(QWidget* parent = nullptr);
signals:
    void clicked(QPointF pixmapPixel);

protected:
    void mousePressEvent(QMouseEvent* event) override;
};

class AnnotationPanel : public QWidget {
    Q_OBJECT
public:
    explicit AnnotationPanel(Session& session, QWidget* parent = nullptr);

    /// The drawing shown by the workspace (null = none) and its current sheet. The pointer
    /// stays valid until the next call.
    void setDrawing(const drawings::DrawingResult* drawing, const std::optional<DrawingRequest>& request,
                    std::size_t sheetIndex);
    /// A click on the current sheet at `paper` (mm, origin bottom-left).
    void sheetClicked(geometry::Vec2 paper);

    /// Selects a dimension of the drawing by id (false when it is not drawn).
    bool selectDimension(const std::string& id);
    const std::string& selectedDimension() const noexcept { return selected_; }
    /// Paper box to highlight on the current sheet (empty when nothing is selected there).
    drawings::Rect2 highlight() const;
    /// Picked corners waiting for the second pick (paper mm on the current sheet).
    std::vector<geometry::Vec2> markers() const;

signals:
    /// The highlight or the markers changed: the preview must be repainted.
    void overlayChanged();

private:
    void refreshSelection();
    void refreshEdits();
    bool run(const core::Result<project::Command>& command);
    std::string drawingId() const;

    Session& session_;
    const drawings::DrawingResult* drawing_ = nullptr;
    std::optional<DrawingRequest> request_;
    std::size_t sheet_ = 0;
    std::string selected_;
    struct Pick {
        std::size_t viewIndex = 0;
        CornerPick corner;
    };
    std::optional<Pick> firstPick_;

    QGroupBox* selectedBox_;
    QLabel* selectedInfo_;
    QDoubleSpinBox* offset_;
    QPushButton* applyOffset_;
    QLineEdit* text_;
    QPushButton* applyText_;
    QPushButton* hide_;
    QPushButton* reset_;
    QComboBox* orientation_;
    QDoubleSpinBox* newOffset_;
    QPushButton* pick_;
    QLabel* pickStatus_;
    QListWidget* edits_;
    QPushButton* removeEdit_;
    QListWidget* findings_;
    QLabel* error_;
};

} // namespace fos::app::gui
