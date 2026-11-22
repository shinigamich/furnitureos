// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/viewport/viewport_widget.hpp"

#include "fos/core/log.hpp"
#include "fos/viewport/camera.hpp"
#include "fos/viewport/scene.hpp"
#include "gl_renderer.hpp"
#include "measure_overlay.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSurfaceFormat>
#include <QWheelEvent>

#include <cmath>

namespace fos::viewport {

namespace {
/// A press-release with less movement than this (logical pixels, Manhattan) is a click.
constexpr int kClickThreshold = 4;

Camera::Preset toCameraPreset(ViewPreset p) {
    switch (p) {
    case ViewPreset::Front: return Camera::Preset::Front;
    case ViewPreset::Back: return Camera::Preset::Back;
    case ViewPreset::Left: return Camera::Preset::Left;
    case ViewPreset::Right: return Camera::Preset::Right;
    case ViewPreset::Top: return Camera::Preset::Top;
    case ViewPreset::Bottom: return Camera::Preset::Bottom;
    case ViewPreset::Isometric: break;
    }
    return Camera::Preset::Isometric;
}
} // namespace

struct ViewportWidget::Impl {
    Scene scene;
    mutable Camera camera;
    DisplayMode mode = DisplayMode::ShadedWithEdges;
    detail::GlRenderer renderer;
    bool glInitialized = false;
    bool geometryDirty = true;
    bool stateDirty = true;

    Qt::MouseButton dragButton = Qt::NoButton;
    QPoint pressPos, lastPos;
    bool dragging = false;
    bool suppressNextClick = false;

    bool measuring = false;
    std::vector<SnapPoint> measurePoints;
    std::optional<SnapPoint> hover;
    QString lengthSuffix = QStringLiteral(" mm");
    QString textureDirectory;

    void updateSceneBounds() {
        // Near/far must contain the parts AND the floor grid (grid spans parts +- >= 1 m).
        geometry::Aabb b = scene.allBounds();
        b.expand(geometry::Vec3{-1000, -1000, 0});
        b.expand(geometry::Vec3{1000, 1000, 0});
        if (!scene.allBounds().empty()) {
            b.expand(scene.allBounds().min - geometry::Vec3{1500, 1500, 0});
            b.expand(scene.allBounds().max + geometry::Vec3{1500, 1500, 0});
        }
        camera.setSceneBounds(b);
    }
};

ViewportWidget::ViewportWidget(QWidget* parent) : QOpenGLWidget(parent), d(std::make_unique<Impl>()) {
    QSurfaceFormat fmt = format();
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8); // section caps
    fmt.setSamples(4);
    setFormat(fmt);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(64, 64);
    d->updateSceneBounds();
}

ViewportWidget::~ViewportWidget() {
    if (d->glInitialized && context()) {
        makeCurrent();
        d->renderer.destroy();
        doneCurrent();
    }
}

void ViewportWidget::setModel(const domain::Model& model, bool fit) {
    d->scene.setModel(model);
    d->updateSceneBounds();
    d->geometryDirty = d->stateDirty = true;
    if (fit) fitAll();
    update();
}

void ViewportWidget::setSelection(const std::vector<std::string>& partIds) {
    d->scene.setSelected(partIds);
    d->stateDirty = true;
    update();
}

void ViewportWidget::setHiddenParts(const std::vector<std::string>& partIds) {
    d->scene.setHidden(partIds);
    d->stateDirty = true;
    update();
}

void ViewportWidget::setIsolatedParts(const std::vector<std::string>& partIds) {
    d->scene.setIsolated(partIds);
    d->stateDirty = true;
    update();
}

void ViewportWidget::setViewPreset(ViewPreset preset) {
    d->camera.setPreset(toCameraPreset(preset));
    fitAll();
}

void ViewportWidget::setProjection(Projection projection) {
    d->camera.setProjection(projection == Projection::Perspective ? CameraProjection::Perspective
                                                                  : CameraProjection::Orthographic);
    update();
}

Projection ViewportWidget::projection() const {
    return d->camera.projection() == CameraProjection::Perspective ? Projection::Perspective
                                                                   : Projection::Orthographic;
}

void ViewportWidget::setDisplayMode(DisplayMode mode) {
    d->mode = mode;
    update();
}

DisplayMode ViewportWidget::displayMode() const { return d->mode; }

void ViewportWidget::fitAll() {
    d->camera.setViewportSize(width(), height());
    d->camera.fit(d->scene.visibleBounds());
    update();
}

std::string ViewportWidget::partAt(const QPoint& position) const {
    d->camera.setViewportSize(width(), height());
    // Sample at the pixel centre.
    const auto ray = d->camera.rayThroughPixel(position.x() + 0.5, position.y() + 0.5);
    const auto hit = d->scene.pick(ray);
    return hit ? d->scene.parts()[*hit].id : std::string();
}

const Camera& ViewportWidget::camera() const {
    d->camera.setViewportSize(width(), height());
    return d->camera;
}

void ViewportWidget::setCamera(const Camera& camera) {
    d->camera = camera;
    d->updateSceneBounds();
    update();
}

void ViewportWidget::setMeasureMode(bool on) {
    if (d->measuring == on) return;
    d->measuring = on;
    d->measurePoints.clear();
    d->hover.reset();
    setMouseTracking(on);
    if (on) {
        setCursor(Qt::CrossCursor);
    } else {
        unsetCursor();
    }
    update();
    emit measureModeChanged(on);
    emit measurementChanged();
}

bool ViewportWidget::measureMode() const { return d->measuring; }

void ViewportWidget::cancelMeasurement() {
    if (d->measurePoints.empty()) return;
    d->measurePoints.clear();
    update();
    emit measurementChanged();
}

std::vector<SnapPoint> ViewportWidget::measurePoints() const { return d->measurePoints; }

std::optional<Measurement> ViewportWidget::measurement() const {
    if (d->measurePoints.size() < 2) return std::nullopt;
    return Measurement{d->measurePoints[0].point, d->measurePoints[1].point};
}

std::optional<SnapPoint> ViewportWidget::hoverSnap() const { return d->hover; }

void ViewportWidget::updateHoverSnap(const QPoint& position) {
    if (!d->measuring) return;
    d->camera.setViewportSize(width(), height());
    const auto snap = snapAt(d->scene, d->camera, position.x() + 0.5, position.y() + 0.5);
    const bool changed = snap.has_value() != d->hover.has_value() ||
                         (snap && (snap->point != d->hover->point || snap->kind != d->hover->kind));
    d->hover = snap;
    if (changed) {
        update();
        emit measurementChanged();
    }
}

void ViewportWidget::setLengthSuffix(const QString& suffix) {
    d->lengthSuffix = suffix;
    update();
}

void ViewportWidget::setSectionPlane(const std::optional<SectionPlane>& plane) {
    d->scene.setSection(plane);
    // Points that are now cut away stay (measurements are independent of the display), but the
    // hover snap is recomputed on the next mouse move.
    d->hover.reset();
    update();
}

std::optional<SectionPlane> ViewportWidget::sectionPlane() const { return d->scene.section(); }

geometry::Aabb ViewportWidget::modelBounds() const { return d->scene.allBounds(); }

void ViewportWidget::setTextureDirectory(const QString& directory) {
    if (directory == d->textureDirectory) return;
    d->textureDirectory = directory;
    d->renderer.setTextureDirectory(directory);
    d->geometryDirty = true;
    update();
}

void ViewportWidget::initializeGL() {
    std::string error;
    d->glInitialized = true;
    if (!d->renderer.initialize(error)) {
        core::logWarning("viewport", error);
    } else {
        const auto f = context()->format();
        core::logInfo("viewport", "OpenGL " + std::to_string(f.majorVersion()) + "." +
                                      std::to_string(f.minorVersion()) + " context, " +
                                      std::to_string(format().samples()) + " MSAA samples requested.");
    }
    d->geometryDirty = d->stateDirty = true;
}

void ViewportWidget::resizeGL(int w, int h) { d->camera.setViewportSize(w, h); }

void ViewportWidget::paintGL() {
    if (d->geometryDirty) {
        d->renderer.uploadGeometry(d->scene);
        d->geometryDirty = false;
    }
    if (d->stateDirty) {
        d->renderer.uploadState(d->scene);
        d->stateDirty = false;
    }
    d->camera.setViewportSize(width(), height());
    const double dpr = devicePixelRatioF();
    d->renderer.render(d->camera, d->mode, static_cast<int>(std::lround(width() * dpr)),
                       static_cast<int>(std::lround(height() * dpr)), dpr, d->scene.section());
    if (d->measuring && (!d->measurePoints.empty() || d->hover)) {
        QPainter painter(this);
        detail::paintMeasureOverlay(painter, d->camera, d->measurePoints, d->hover, d->lengthSuffix);
    }
}

void ViewportWidget::mousePressEvent(QMouseEvent* event) {
    if (d->dragButton != Qt::NoButton) return; // a drag with another button is in progress
    const Qt::MouseButton b = event->button();
    if (b != Qt::LeftButton && b != Qt::MiddleButton) return;
    d->dragButton = b;
    d->pressPos = d->lastPos = event->position().toPoint();
    d->dragging = false;
    event->accept();
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event) {
    if (d->dragButton == Qt::NoButton) {
        updateHoverSnap(event->position().toPoint());
        return;
    }
    const QPoint p = event->position().toPoint();
    if (!d->dragging && (p - d->pressPos).manhattanLength() < kClickThreshold) return;
    if (!d->dragging) {
        d->dragging = true;
        d->lastPos = d->pressPos;
    }
    const QPoint delta = p - d->lastPos;
    d->lastPos = p;
    d->camera.setViewportSize(width(), height());
    const bool panning = d->dragButton == Qt::MiddleButton || (event->modifiers() & Qt::ShiftModifier);
    if (panning)
        d->camera.pan(delta.x(), delta.y());
    else
        d->camera.orbit(delta.x(), delta.y());
    update();
    event->accept();
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != d->dragButton) return;
    const bool wasClick = !d->dragging && d->dragButton == Qt::LeftButton;
    d->dragButton = Qt::NoButton;
    d->dragging = false;
    if (wasClick && !d->suppressNextClick && d->measuring) {
        updateHoverSnap(event->position().toPoint());
        if (d->hover) {
            if (d->measurePoints.size() >= 2) d->measurePoints.clear();
            d->measurePoints.push_back(*d->hover);
            update();
            emit measurementChanged();
        }
    } else if (wasClick && !d->suppressNextClick) {
        const bool additive = event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
        emit partClicked(QString::fromStdString(partAt(event->position().toPoint())), additive);
    }
    d->suppressNextClick = false;
    event->accept();
}

void ViewportWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    // The release that follows a double click must not emit a second partClicked.
    d->dragButton = Qt::LeftButton;
    d->pressPos = d->lastPos = event->position().toPoint();
    d->dragging = false;
    d->suppressNextClick = true;
    if (d->measuring) { // no part action; the release adds no second point at the same spot
        event->accept();
        return;
    }
    const std::string id = partAt(event->position().toPoint());
    if (!id.empty()) emit partDoubleClicked(QString::fromStdString(id));
    event->accept();
}

void ViewportWidget::wheelEvent(QWheelEvent* event) {
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0) return;
    d->camera.setViewportSize(width(), height());
    const QPointF p = event->position();
    d->camera.zoomAt(p.x(), p.y(), std::pow(0.85, steps));
    update();
    event->accept();
}

void ViewportWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && d->measuring) {
        if (!d->measurePoints.empty())
            cancelMeasurement();
        else
            setMeasureMode(false);
        event->accept();
        return;
    }
    QOpenGLWidget::keyPressEvent(event);
}

void ViewportWidget::leaveEvent(QEvent* event) {
    if (d->hover) {
        d->hover.reset();
        update();
        emit measurementChanged();
    }
    QOpenGLWidget::leaveEvent(event);
}

} // namespace fos::viewport
