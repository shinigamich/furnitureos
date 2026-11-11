// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/geometry/ray.hpp"

#include <cmath>
#include <limits>

namespace fos::geometry {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

bool validRay(const Ray3& r) noexcept {
    const Vec3 d = r.direction, o = r.origin;
    return std::isfinite(d.x) && std::isfinite(d.y) && std::isfinite(d.z) && std::isfinite(o.x) &&
           std::isfinite(o.y) && std::isfinite(o.z) && (d.x != 0.0 || d.y != 0.0 || d.z != 0.0);
}

double comp(Vec3 v, int axis) noexcept { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }

/// Slab intersection of the full line with [lo,hi]; reports the axes that bound entry/exit and
/// whether entry/exit happen through the min (0) or max (1) plane of that axis.
struct Slabs {
    double tEnter = -kInf, tExit = kInf;
    int enterAxis = -1, exitAxis = -1;
    bool enterAtMax = false, exitAtMax = false;
    bool hit = true;
};

Slabs slabs(const Ray3& ray, Vec3 lo, Vec3 hi) noexcept {
    Slabs s;
    for (int axis = 0; axis < 3; ++axis) {
        const double o = comp(ray.origin, axis), d = comp(ray.direction, axis);
        const double mn = comp(lo, axis), mx = comp(hi, axis);
        if (d == 0.0) {
            if (o < mn || o > mx) s.hit = false;
            continue;
        }
        double t0 = (mn - o) / d, t1 = (mx - o) / d;
        bool nearIsMax = false;
        if (t0 > t1) {
            const double tmp = t0;
            t0 = t1;
            t1 = tmp;
            nearIsMax = true;
        }
        if (t0 > s.tEnter) {
            s.tEnter = t0;
            s.enterAxis = axis;
            s.enterAtMax = nearIsMax;
        }
        if (t1 < s.tExit) {
            s.tExit = t1;
            s.exitAxis = axis;
            s.exitAtMax = !nearIsMax;
        }
    }
    if (s.tEnter > s.tExit || s.tExit < 0.0) s.hit = false;
    return s;
}

BoxFace faceOf(int axis, bool atMax) noexcept {
    switch (axis) {
    case 0: return atMax ? BoxFace::XMax : BoxFace::XMin;
    case 1: return atMax ? BoxFace::YMax : BoxFace::YMin;
    default: return atMax ? BoxFace::ZMax : BoxFace::ZMin;
    }
}

} // namespace

std::optional<RayRange> intersectRayAabb(const Ray3& ray, const Aabb& box) noexcept {
    if (box.empty() || !validRay(ray)) return std::nullopt;
    const Slabs s = slabs(ray, box.min, box.max);
    if (!s.hit) return std::nullopt;
    return RayRange{s.tEnter > 0.0 ? s.tEnter : 0.0, s.tExit};
}

std::optional<TriangleHit> intersectRayTriangle(const Ray3& ray, Vec3 a, Vec3 b, Vec3 c,
                                                bool cullBackFaces) noexcept {
    if (!validRay(ray)) return std::nullopt;
    constexpr double kBary = 1e-9;
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 pvec = cross(ray.direction, e2);
    const double det = dot(e1, pvec);
    // Parallel (or degenerate triangle): |det| = |d| * 2 * area * |cos(angle to normal)|.
    const double scale = length(ray.direction) * length(e1) * length(e2);
    if (std::fabs(det) <= 1e-12 * scale || det == 0.0) return std::nullopt;
    if (cullBackFaces && det < 0.0) return std::nullopt;
    const double inv = 1.0 / det;
    const Vec3 tvec = ray.origin - a;
    const double u = dot(tvec, pvec) * inv;
    if (u < -kBary || u > 1.0 + kBary) return std::nullopt;
    const Vec3 qvec = cross(tvec, e1);
    const double v = dot(ray.direction, qvec) * inv;
    if (v < -kBary || u + v > 1.0 + kBary) return std::nullopt;
    const double t = dot(e2, qvec) * inv;
    if (!(t >= 0.0)) return std::nullopt;
    return TriangleHit{t, u, v, det > 0.0};
}

std::optional<MeshHit> intersectRayMesh(const Ray3& ray, const TriangleMesh& mesh, bool cullBackFaces) noexcept {
    std::optional<MeshHit> best;
    const std::size_t count = mesh.positions.size();
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t ia = mesh.indices[i], ib = mesh.indices[i + 1], ic = mesh.indices[i + 2];
        if (ia >= count || ib >= count || ic >= count) continue;
        const Vec3 a = mesh.positions[ia], b = mesh.positions[ib], c = mesh.positions[ic];
        const auto hit = intersectRayTriangle(ray, a, b, c, cullBackFaces);
        if (!hit || (best && hit->t >= best->t)) continue;
        best = MeshHit{hit->t, static_cast<std::uint32_t>(i / 3), ray.at(hit->t), normalized(cross(b - a, c - a)),
                       hit->frontFace};
    }
    return best;
}

std::optional<BoxHit> intersectRayOrientedBox(const Ray3& ray, Vec3 size, const Transform& placement) noexcept {
    if (!validRay(ray) || !(size.x > 0.0) || !(size.y > 0.0) || !(size.z > 0.0)) return std::nullopt;
    const Transform toLocal = placement.inverse();
    const Ray3 local{toLocal.applyPoint(ray.origin), toLocal.applyVector(ray.direction)};
    const Slabs s = slabs(local, {0, 0, 0}, size);
    if (!s.hit) return std::nullopt;
    const bool outside = s.tEnter >= 0.0 && s.enterAxis >= 0;
    const double t = outside ? s.tEnter : s.tExit;
    const int axis = outside ? s.enterAxis : s.exitAxis;
    if (axis < 0 || !std::isfinite(t)) return std::nullopt;
    const bool atMax = outside ? s.enterAtMax : s.exitAtMax;
    const BoxFace face = faceOf(axis, atMax);
    // Snap the local point onto the face plane and into the box to remove rounding noise.
    Vec3 lp = local.at(t);
    const auto clamp = [](double v, double hi) { return v < 0.0 ? 0.0 : (v > hi ? hi : v); };
    lp = {clamp(lp.x, size.x), clamp(lp.y, size.y), clamp(lp.z, size.z)};
    if (axis == 0) lp.x = atMax ? size.x : 0.0;
    if (axis == 1) lp.y = atMax ? size.y : 0.0;
    if (axis == 2) lp.z = atMax ? size.z : 0.0;
    return BoxHit{t, face, ray.at(t), lp, placement.applyVector(boxFaceNormal(face))};
}

} // namespace fos::geometry
