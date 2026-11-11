// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/projection.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace fos::geometry {

namespace {

Vec3 boxCorner(Vec3 size, int i) noexcept {
    return {(i & 1) ? size.x : 0.0, (i & 2) ? size.y : 0.0, (i & 4) ? size.z : 0.0};
}

bool usable(Vec3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && length(v) > 0.0;
}

} // namespace

bool ViewBasis::isOrthonormal(double eps) const noexcept {
    return core::approxEqual(dot(right, right), 1, eps) && core::approxEqual(dot(up, up), 1, eps) &&
           core::approxEqual(dot(towardViewer, towardViewer), 1, eps) && core::approxZero(dot(right, up), eps) &&
           approxEqual(cross(right, up), towardViewer, eps);
}

ViewBasis frontView() noexcept { return {{1, 0, 0}, {0, 0, 1}, {0, -1, 0}}; }
ViewBasis backView() noexcept { return {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}; }
ViewBasis leftView() noexcept { return {{0, -1, 0}, {0, 0, 1}, {-1, 0, 0}}; }
ViewBasis rightView() noexcept { return {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}}; }
ViewBasis topView() noexcept { return {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; }
ViewBasis bottomView() noexcept { return {{1, 0, 0}, {0, -1, 0}, {0, 0, -1}}; }

core::Result<ViewBasis> makeViewBasis(Vec3 towardViewer, Vec3 upHint) {
    if (!usable(towardViewer) || !usable(upHint))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "View basis needs non-zero, finite 'toward viewer' and 'up' vectors.");
    const Vec3 w = normalized(towardViewer);
    const Vec3 r = cross(normalized(upHint), w);
    if (length(r) < 1e-9)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "View basis is undefined: the 'up' vector is parallel to the viewing direction.");
    const Vec3 right = normalized(r);
    return ViewBasis{right, cross(w, right), w};
}

ProjectedPoint project(const ViewBasis& view, Vec3 point) noexcept {
    return {{dot(point, view.right), dot(point, view.up)}, dot(point, view.towardViewer)};
}

Polygon2 projectBoxSilhouette(const ViewBasis& view, Vec3 size, const Transform& placement, double eps) {
    std::vector<Vec2> pts;
    pts.reserve(8);
    for (int i = 0; i < 8; ++i) pts.push_back(project(view, placement.applyPoint(boxCorner(size, i))).position);
    return convexHull(pts, eps);
}

DepthRange projectBoxDepthRange(const ViewBasis& view, Vec3 size, const Transform& placement) noexcept {
    DepthRange r{};
    for (int i = 0; i < 8; ++i) {
        const double d = project(view, placement.applyPoint(boxCorner(size, i))).depth;
        r.farthest = i == 0 ? d : std::min(r.farthest, d);
        r.nearest = i == 0 ? d : std::max(r.nearest, d);
    }
    return r;
}

} // namespace fos::geometry
