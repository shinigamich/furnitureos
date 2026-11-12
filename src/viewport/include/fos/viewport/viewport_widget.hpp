// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/model.hpp"

#include <QOpenGLWidget>

#include <memory>
#include <string>
#include <vector>

/// 3D viewport of a resolved engineering model (Qt 6 Widgets + OpenGL).
///
/// CONTRACT between module `viewport` (owns the implementation) and module `app`
/// (uses it). Fixed by the integration lead; extend only by adding members.
///
/// * The widget only DISPLAYS a domain::Model; it never modifies it and never defines
///   authoritative geometry (ARCHITECTURE.md §3). Parts are drawn from Part size +
///   Part::placement + Assembly::placement (project frame, Z up, mm).
/// * Selection is identified by part id strings (domain::PartId::str()). The widget
///   emits requests; the application owns the selection state and pushes it back with
///   setSelection().
namespace fos::viewport {

enum class ViewPreset { Front, Back, Left, Right, Top, Bottom, Isometric };
enum class Projection { Perspective, Orthographic };
enum class DisplayMode { Shaded, ShadedWithEdges, Wireframe };

class ViewportWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget() override;

    /// Replace the displayed model (copied). Keeps the camera unless `fit` is true.
    void setModel(const domain::Model& model, bool fit = false);
    /// Highlight exactly these parts (others unselected).
    void setSelection(const std::vector<std::string>& partIds);
    /// Parts not drawn (and not pickable).
    void setHiddenParts(const std::vector<std::string>& partIds);
    /// When non-empty, only these parts are drawn ("isolate"); empty = all.
    void setIsolatedParts(const std::vector<std::string>& partIds);

    void setViewPreset(ViewPreset preset);
    void setProjection(Projection projection);
    Projection projection() const;
    void setDisplayMode(DisplayMode mode);
    DisplayMode displayMode() const;
    /// Frame all visible parts.
    void fitAll();

    /// Part id under the widget-local pixel position, empty if none (CPU ray picking on
    /// the part boxes; independent of the GL state, usable in tests).
    std::string partAt(const QPoint& position) const;

signals:
    /// Left click on a part (empty id = click on empty space). `additive` = Ctrl/Shift held.
    void partClicked(const QString& partId, bool additive);
    /// Double click on a part (application typically isolates or edits it).
    void partDoubleClicked(const QString& partId);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace fos::viewport
