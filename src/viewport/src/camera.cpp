// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/viewport/camera.hpp"

#include <algorithm>
#include <cmath>

namespace fos::viewport {
using geometry::Vec2;
using geometry::Vec3;

namespace {
double tanHalfFov() noexcept { return std::tan(core::degToRad(Camera::kFovYDeg) * 0.5); }
} // namespace

Camera::Camera() { setPreset(Preset::Isometric); }

void Camera::setOrientation(double yawDeg, double pitchDeg) noexcept {
    if (!std::isfinite(yawDeg) || !std::isfinite(pitchDeg)) return;
    yaw_ = std::remainder(yawDeg, 360.0);
    pitch_ = std::clamp(pitchDeg, -90.0, 90.0);
}

geometry::ViewBasis Camera::basis() const noexcept {
    const double y = core::degToRad(yaw_), p = core::degToRad(pitch_);
    geometry::ViewBasis b;
    b.towardViewer = {std::sin(y) * std::cos(p), -std::cos(y) * std::cos(p), std::sin(p)};
    b.right = {std::cos(y), std::sin(y), 0};
    b.up = cross(b.towardViewer, b.right);
    return b;
}

Vec3 Camera::viewDirection() const noexcept { return -basis().towardViewer; }

void Camera::setDistance(double distance) noexcept {
    if (!std::isfinite(distance)) return;
    distance_ = std::clamp(distance, kMinDistance, kMaxDistance);
}

Vec3 Camera::eye() const noexcept { return target_ + basis().towardViewer * distance_; }

void Camera::setViewportSize(int width, int height) noexcept {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
}

double Camera::halfHeightAtTarget() const noexcept { return distance_ * tanHalfFov(); }

double Camera::nearPlane() const noexcept {
    const Vec3 c = scene_.empty() ? target_ : scene_.center();
    const double r = scene_.empty() ? 1000.0 : std::max(length(scene_.size()) * 0.5, 1.0);
    const double dc = dot(c - eye(), viewDirection());
    const double n = dc - r * 1.05;
    if (projection_ == CameraProjection::Orthographic) return n;
    return std::max({n, farPlane() * 1e-4, 0.1});
}

double Camera::farPlane() const noexcept {
    const Vec3 c = scene_.empty() ? target_ : scene_.center();
    const double r = scene_.empty() ? 1000.0 : std::max(length(scene_.size()) * 0.5, 1.0);
    const double dc = dot(c - eye(), viewDirection());
    return std::max(dc + r * 1.05, 1.0);
}

Mat4 Camera::viewMatrix() const noexcept {
    const auto b = basis();
    const Vec3 e = eye();
    const Vec3 rows[3] = {b.right, b.up, b.towardViewer};
    Mat4 m{};
    for (int r = 0; r < 3; ++r) {
        m[0 * 4 + r] = static_cast<float>(rows[r].x);
        m[1 * 4 + r] = static_cast<float>(rows[r].y);
        m[2 * 4 + r] = static_cast<float>(rows[r].z);
        m[3 * 4 + r] = static_cast<float>(-dot(rows[r], e));
    }
    m[15] = 1.0f;
    return m;
}

Mat4 Camera::projectionMatrix() const noexcept {
    const double n = nearPlane(), f = farPlane();
    Mat4 m{};
    if (projection_ == CameraProjection::Perspective) {
        const double t = 1.0 / tanHalfFov();
        m[0] = static_cast<float>(t / aspect());
        m[5] = static_cast<float>(t);
        m[10] = static_cast<float>((f + n) / (n - f));
        m[11] = -1.0f;
        m[14] = static_cast<float>(2.0 * f * n / (n - f));
    } else {
        const double hh = halfHeightAtTarget(), hw = hh * aspect();
        m[0] = static_cast<float>(1.0 / hw);
        m[5] = static_cast<float>(1.0 / hh);
        m[10] = static_cast<float>(-2.0 / (f - n));
        m[14] = static_cast<float>(-(f + n) / (f - n));
        m[15] = 1.0f;
    }
    return m;
}

geometry::Ray3 Camera::rayThroughPixel(double px, double py) const noexcept {
    const auto b = basis();
    const double nx = 2.0 * px / width_ - 1.0;
    const double ny = 1.0 - 2.0 * py / height_;
    const Vec3 dir = -b.towardViewer;
    if (projection_ == CameraProjection::Perspective) {
        const double t = tanHalfFov();
        const Vec3 d = dir + b.right * (nx * t * aspect()) + b.up * (ny * t);
        return {eye(), normalized(d)};
    }
    const double hh = halfHeightAtTarget(), hw = hh * aspect();
    const Vec3 o = eye() + b.right * (nx * hw) + b.up * (ny * hh) + dir * nearPlane();
    return {o, dir};
}

std::optional<Vec2> Camera::projectToPixel(Vec3 world) const noexcept {
    const auto b = basis();
    const Vec3 v = world - eye();
    const double z = -dot(v, b.towardViewer);
    const double x = dot(v, b.right), y = dot(v, b.up);
    double nx = 0, ny = 0;
    if (projection_ == CameraProjection::Perspective) {
        if (z <= 0) return std::nullopt;
        const double t = tanHalfFov();
        nx = x / (z * t * aspect());
        ny = y / (z * t);
    } else {
        const double hh = halfHeightAtTarget();
        nx = x / (hh * aspect());
        ny = y / hh;
    }
    return Vec2{(nx + 1.0) * 0.5 * width_, (1.0 - ny) * 0.5 * height_};
}

void Camera::orbit(double dxPixels, double dyPixels, double degPerPixel) noexcept {
    setOrientation(yaw_ - dxPixels * degPerPixel, pitch_ + dyPixels * degPerPixel);
}

void Camera::pan(double dxPixels, double dyPixels) noexcept {
    const auto b = basis();
    const double wpp = 2.0 * halfHeightAtTarget() / height_;
    target_ = target_ - b.right * (dxPixels * wpp) + b.up * (dyPixels * wpp);
}

void Camera::zoomAt(double px, double py, double factor) noexcept {
    if (!std::isfinite(factor) || factor <= 0) return;
    const double newDistance = std::clamp(distance_ * factor, kMinDistance, kMaxDistance);
    factor = newDistance / distance_;
    const auto b = basis();
    const double nx = 2.0 * px / width_ - 1.0, ny = 1.0 - 2.0 * py / height_;
    const double hh = halfHeightAtTarget();
    const Vec3 p = target_ + b.right * (nx * hh * aspect()) + b.up * (ny * hh);
    target_ = p + (target_ - p) * factor;
    distance_ = newDistance;
}

void Camera::setPreset(Preset preset) noexcept {
    switch (preset) {
    case Preset::Front: setOrientation(0, 0); break;
    case Preset::Back: setOrientation(180, 0); break;
    case Preset::Left: setOrientation(-90, 0); break;
    case Preset::Right: setOrientation(90, 0); break;
    case Preset::Top: setOrientation(0, 90); break;
    case Preset::Bottom: setOrientation(0, -90); break;
    case Preset::Isometric: setOrientation(45, core::radToDeg(std::atan(1.0 / std::sqrt(2.0)))); break;
    }
}

void Camera::fit(const geometry::Aabb& boxIn) noexcept {
    geometry::Aabb box = boxIn;
    if (box.empty()) box = {{-500, -500, -500}, {500, 500, 500}};
    const auto b = basis();
    const Vec3 c = box.center();
    double maxR = 0, maxU = 0, maxD = 0;
    for (int i = 0; i < 8; ++i) {
        const Vec3 p{(i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y,
                     (i & 4) ? box.max.z : box.min.z};
        const Vec3 v = p - c;
        maxR = std::max(maxR, std::abs(dot(v, b.right)));
        maxU = std::max(maxU, std::abs(dot(v, b.up)));
        maxD = std::max(maxD, std::abs(dot(v, b.towardViewer)));
    }
    const double hh = std::max({maxU, maxR / aspect(), 1.0}) * 1.05;
    double d = hh / tanHalfFov();
    if (projection_ == CameraProjection::Perspective) d += maxD;
    target_ = c;
    setDistance(d);
}

} // namespace fos::viewport
