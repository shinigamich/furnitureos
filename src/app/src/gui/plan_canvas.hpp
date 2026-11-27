// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// The drawing surface of the Plan workspace: top-down footprints (project XY, mm; +Y up on
/// screen), names, sizes, X gaps, overall dimensions and the optional room. Zoom (wheel, about
/// the cursor), pan (middle or right button drag), select (left click) and drag-to-move with
/// snapping (fos::app::snapPlanMove). The canvas never edits the project: it emits
/// itemClicked / moveRequested and the workspace turns them into commands.
#include "fos/app/plan_view.hpp"

#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <QWidget>

#include <optional>
#include <string>
#include <vector>

class QPainter;

namespace fos::app::gui {

class PlanCanvas : public QWidget {
    Q_OBJECT
public:
    /// Snap distance on screen, pixels.
    static constexpr double kSnapPixels = 8.0;

    explicit PlanCanvas(QWidget* parent = nullptr);

    void setFootprints(std::vector<PlanFootprint> footprints);
    void setRoom(std::optional<project::RoomSettings> room);
    void setSelectedItem(const std::string& itemId);
    /// Further selected items of a multi-selection (highlighted and dragged together).
    void setExtraSelectedItems(std::vector<std::string> itemIds);
    const std::vector<PlanFootprint>& footprints() const noexcept { return footprints_; }
    const std::optional<project::RoomSettings>& room() const noexcept { return room_; }

    /// Fits the items and the room (walls included) into the widget.
    void fitToView();
    void zoomBy(double factor);
    double pixelsPerMm() const noexcept { return scale_; }

    /// Writes the plan as SVG (1 SVG user unit = 1 mm). Returns false when the file cannot be written.
    bool exportSvg(const QString& path, const QString& title) const;

    /// Paints the plan with `scale` px per mm; project point (0,0) at device `origin` (+Y up).
    /// `textPixels` is the label font size in device pixels.
    void render(QPainter& painter, double scale, QPointF origin, double textPixels, bool interactive) const;

signals:
    /// Left click: the item under the cursor, or empty for empty space.
    void itemClicked(const QString& itemId);
    /// A drag ended with a non-zero (snapped) translation, mm.
    void moveRequested(const QString& itemId, double dx, double dy);
    /// Ctrl+click on an item: add it to / remove it from the selection.
    void itemToggled(const QString& itemId);
    /// Arrow key: move the selection by (dx, dy) mm (1 mm, with Shift 10 mm).
    void nudgeRequested(double dx, double dy);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    geometry::Vec2 toProject(QPointF screen) const;
    geometry::Rect2 contentRect() const;

    std::vector<PlanFootprint> footprints_;
    std::vector<QPainterPath> paths_;  ///< Union of each footprint's outlines (project mm).
    std::optional<project::RoomSettings> room_;
    std::string selected_;
    std::vector<std::string> extraSelected_;
    bool isSelected(const std::string& itemId) const;
    double scale_ = 0.2;               ///< px per mm.
    QPointF origin_{40, 400};          ///< Screen position of project (0,0).
    bool fitted_ = false;

    enum class Drag { None, Pan, Move };
    Drag drag_ = Drag::None;
    QPointF pressPos_;
    QPointF lastPos_;
    std::string dragItem_;
    std::optional<PlanSnap> snap_;
};

} // namespace fos::app::gui
