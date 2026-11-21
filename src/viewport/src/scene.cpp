// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/viewport/scene.hpp"

#include "fos/geometry/mesh.hpp"
#include "fos/viewport/shape_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fos::viewport {
using geometry::Vec3;

namespace {
std::vector<std::string> sortedUnique(std::vector<std::string> v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}
bool contains(const std::vector<std::string>& sorted, const std::string& id) {
    return std::binary_search(sorted.begin(), sorted.end(), id);
}
bool validSize(Vec3 s) {
    return std::isfinite(s.x) && std::isfinite(s.y) && std::isfinite(s.z) && s.x > 0 && s.y > 0 && s.z > 0;
}
} // namespace

void Scene::setModel(const domain::Model& model) {
    parts_.clear();
    valid_.clear();
    meshes_.clear();
    edges_.clear();
    diagnostics_.clear();
    for (const auto& a : model.assemblies) {
        for (const auto& p : a.parts) {
            ScenePart sp;
            sp.id = p.id.str();
            sp.size = {p.length, p.width, p.thickness};
            sp.world = a.placement * p.placement;
            const domain::Material* m = model.findMaterial(p.material);
            sp.color = m ? m->color : kFallbackColor;
            const bool ok = validSize(sp.size);
            geometry::TriangleMesh mesh;
            std::vector<geometry::Edge3> edges;
            if (ok && domain::hasShape(p) && !domain::isRectangularOutline(p)) {
                auto g = makeShapedPartGeometry(domain::finishedContour(p), p.shape->cutouts, p.thickness,
                                                shapeChordTolerance(p.length, p.width));
                if (g.ok()) {
                    sp.contoured = true;
                    mesh = std::move(g.value().mesh);
                    edges = std::move(g.value().edges);
                } else {
                    diagnostics_.push_back({core::Severity::Warning, "viewport.shape.fallback_box",
                                            "Part '" + p.name + "' is shown as its " + core::formatLength(p.length) +
                                                " x " + core::formatLength(p.width) +
                                                " mm bounding box because its contour cannot be displayed: " +
                                                g.error().message,
                                            sp.id});
                }
            }
            if (ok && !sp.contoured) {
                mesh = geometry::makeBox(sp.size);
                edges = geometry::makeBoxEdges(sp.size);
            }
            valid_.push_back(ok);
            meshes_.push_back(std::move(mesh));
            edges_.push_back(std::move(edges));
            parts_.push_back(std::move(sp));
        }
    }
}

void Scene::setHidden(const std::vector<std::string>& ids) { hidden_ = sortedUnique(ids); }
void Scene::setIsolated(const std::vector<std::string>& ids) { isolated_ = sortedUnique(ids); }
void Scene::setSelected(const std::vector<std::string>& ids) { selected_ = sortedUnique(ids); }

bool Scene::isVisible(std::size_t i) const noexcept {
    if (i >= parts_.size() || !valid_[i]) return false;
    const std::string& id = parts_[i].id;
    if (contains(hidden_, id)) return false;
    return isolated_.empty() || contains(isolated_, id);
}

bool Scene::isSelected(std::size_t i) const noexcept { return i < parts_.size() && contains(selected_, parts_[i].id); }

geometry::Aabb Scene::visibleBounds() const {
    geometry::Aabb b;
    for (std::size_t i = 0; i < parts_.size(); ++i)
        if (isVisible(i)) b.expand(geometry::Aabb::fromSize(parts_[i].size).transformed(parts_[i].world));
    return b;
}

geometry::Aabb Scene::allBounds() const {
    geometry::Aabb b;
    for (std::size_t i = 0; i < parts_.size(); ++i)
        if (valid_[i]) b.expand(geometry::Aabb::fromSize(parts_[i].size).transformed(parts_[i].world));
    return b;
}

namespace {
/// Ray parameter interval [lo, hi] (hi may be +inf) on the kept side of a plane whose signed
/// distance along the ray is s(t) = s0 + t * ds; empty when lo > hi.
struct Interval {
    double lo = 0, hi = std::numeric_limits<double>::infinity();
    bool empty() const noexcept { return lo > hi; }
};
Interval keptInterval(double s0, double ds) {
    Interval k;
    if (ds == 0) {
        if (s0 < 0) k.lo = 1, k.hi = 0; // parallel, entirely on the removed side
        return k;
    }
    const double tPlane = -s0 / ds;
    if (ds > 0)
        k.lo = std::max(0.0, tPlane);
    else
        k.hi = tPlane; // lo stays 0; empty when tPlane < 0
    return k;
}
} // namespace

bool Scene::isSectionRemoved(std::size_t i) const noexcept {
    if (!section_ || i >= parts_.size()) return false;
    return classify(*section_, geometry::Aabb::fromSize(parts_[i].size).transformed(parts_[i].world)) ==
           SectionSide::Removed;
}

std::optional<Scene::PickHit> Scene::pickHit(const geometry::Ray3& ray) const {
    std::optional<PickHit> best;
    for (std::size_t i = 0; i < parts_.size(); ++i) {
        if (!isVisible(i)) continue;
        const ScenePart& part = parts_[i];
        SectionSide side = SectionSide::Kept;
        if (section_) side = classify(*section_, geometry::Aabb::fromSize(part.size).transformed(part.world));
        if (side == SectionSide::Removed) continue;
        // Rigid transform: the ray parameter is the same in the part frame.
        const geometry::Transform inv = part.world.inverse();
        const geometry::Ray3 local{inv.applyPoint(ray.origin), inv.applyVector(ray.direction)};
        std::optional<double> t;
        if (side == SectionSide::Kept) {
            if (part.contoured) {
                if (!geometry::intersectRayAabb(local, geometry::Aabb::fromSize(part.size))) continue;
                if (const auto mh = geometry::intersectRayMesh(local, meshes_[i])) t = mh->t;
            } else if (const auto hit = geometry::intersectRayOrientedBox(ray, part.size, part.world)) {
                t = hit->t;
            }
        } else {
            const Interval k = keptInterval(section_->signedDistance(ray.origin),
                                            dot(section_->keptNormal(), ray.direction));
            if (k.empty()) continue;
            if (!part.contoured) {
                // Convex box: the visible point is the later of box entry and plane crossing.
                const auto r = geometry::intersectRayAabb(local, geometry::Aabb::fromSize(part.size));
                if (r && std::max(r->tMin, k.lo) <= std::min(r->tMax, k.hi)) t = std::max(r->tMin, k.lo);
            } else if (k.lo > 0) {
                // The ray enters the kept side at k.lo: a back face as the first hit beyond the
                // plane means the plane point is inside the material (the cap is hit).
                const geometry::Ray3 from{local.at(k.lo), local.direction};
                if (const auto mh = geometry::intersectRayMesh(from, meshes_[i]))
                    t = mh->frontFace ? k.lo + mh->t : k.lo;
            } else if (const auto mh = geometry::intersectRayMesh(local, meshes_[i]); mh && mh->t <= k.hi) {
                t = mh->t;
            }
        }
        if (t && (!best || *t < best->t)) best = PickHit{i, *t, ray.at(*t)};
    }
    return best;
}

std::optional<std::size_t> Scene::pick(const geometry::Ray3& ray) const {
    const auto hit = pickHit(ray);
    return hit ? std::optional<std::size_t>(hit->index) : std::nullopt;
}

const std::vector<geometry::Edge3>& Scene::localEdges(std::size_t index) const {
    static const std::vector<geometry::Edge3> kEmpty;
    return index < edges_.size() ? edges_[index] : kEmpty;
}

const geometry::TriangleMesh& Scene::localMesh(std::size_t index) const {
    static const geometry::TriangleMesh kEmpty;
    return index < meshes_.size() ? meshes_[index] : kEmpty;
}

Scene::Buffers Scene::buildBuffers() const {
    Buffers out;
    std::size_t n = 0;
    for (bool v : valid_) n += v ? 1 : 0;
    out.vertices.reserve(n * 24);
    out.indices.reserve(n * 36);
    out.lines.reserve(n * 24);
    auto f = [](double v) { return static_cast<float>(v); };
    for (std::size_t i = 0; i < parts_.size(); ++i) {
        if (!valid_[i]) continue;
        const auto& p = parts_[i];
        const float idx = static_cast<float>(i);
        const auto mesh = geometry::transformed(meshes_[i], p.world);
        const auto base = static_cast<std::uint32_t>(out.vertices.size());
        for (std::size_t v = 0; v < mesh.positions.size(); ++v) {
            const Vec3 q = mesh.positions[v], nn = mesh.normals[v];
            out.vertices.push_back({f(q.x), f(q.y), f(q.z), f(nn.x), f(nn.y), f(nn.z), idx});
        }
        for (auto k : mesh.indices) out.indices.push_back(base + k);
        for (const auto& e : geometry::transformed(edges_[i], p.world)) {
            out.lines.push_back({f(e.a.x), f(e.a.y), f(e.a.z), idx});
            out.lines.push_back({f(e.b.x), f(e.b.y), f(e.b.z), idx});
        }
    }
    return out;
}

} // namespace fos::viewport
