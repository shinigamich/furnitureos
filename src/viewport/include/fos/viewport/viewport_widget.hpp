// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/model.hpp"
#include "fos/viewport/section.hpp"
#include "fos/viewport/snap.hpp"

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

class Camera;

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

    // --- Additions by module `viewport` (not part of the original contract) ---------------
    /// Current camera (Qt-free math, see fos/viewport/camera.hpp), viewport size synced to
    /// the widget's logical size. Useful for tests and for saving/restoring views.
    const Camera& camera() const;
    /// Replace the camera (orientation, target, distance, projection).
    void setCamera(const Camera& camera);

    // --- Measurement tool (phase 15; display only, never changes the model) ---------------
    /// In measure mode the cursor snaps (fos/viewport/snap.hpp) and two left clicks give a
    /// distance; clicks then never emit partClicked. Dragging still orbits/pans. Esc clears the
    /// current points, a second Esc leaves measure mode (measureModeChanged(false)). A third
    /// click starts a new measurement. Leaving the mode clears the points.
    void setMeasureMode(bool on);
    bool measureMode() const;
    /// Clears the measured points (stays in measure mode).
    void cancelMeasurement();
    /// Picked points (0, 1 or 2), project frame, mm.
    std::vector<SnapPoint> measurePoints() const;
    /// The completed measurement (two points), else nullopt.
    std::optional<Measurement> measurement() const;
    /// Snap point under the cursor in measure mode (nullopt over empty space / not measuring).
    std::optional<SnapPoint> hoverSnap() const;
    /// Simulates the cursor at widget-local `position` in measure mode: updates hoverSnap()
    /// exactly as a mouse move does (also used by tests without a GL context).
    void updateHoverSnap(const QPoint& position);
    /// Unit suffix of the in-view distance label (default " mm"; the app passes a translation).
    void setLengthSuffix(const QString& suffix);

    // --- Section view (phase 15; display only) ----------------------------------------------
    /// Axis-aligned clipping plane (nullopt = off). Geometry on the removed side is clipped in
    /// the shader (gl_ClipDistance); cut faces get a flat cap colour (stencil caps) when the
    /// framebuffer has a stencil buffer, otherwise the clipped geometry is drawn open with
    /// back faces visible. Picking (partAt, clicks) and snapping ignore removed geometry.
    void setSectionPlane(const std::optional<SectionPlane>& plane);
    std::optional<SectionPlane> sectionPlane() const;
    /// Bounds of all parts (project frame, mm; empty without parts), e.g. for the slider range.
    geometry::Aabb modelBounds() const;

signals:
    /// Left click on a part (empty id = click on empty space). `additive` = Ctrl/Shift held.
    void partClicked(const QString& partId, bool additive);
    /// Double click on a part (application typically isolates or edits it).
    void partDoubleClicked(const QString& partId);
    /// Measure mode: points or the hover snap changed.
    void measurementChanged();
    /// Measure mode switched (also when Esc leaves it).
    void measureModeChanged(bool on);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace fos::viewport
