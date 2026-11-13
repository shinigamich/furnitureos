// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/viewport/scene.hpp"

#include "fos/geometry/mesh.hpp"

#include <algorithm>
#include <cmath>

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
    for (const auto& a : model.assemblies) {
        for (const auto& p : a.parts) {
            ScenePart sp;
            sp.id = p.id.str();
            sp.size = {p.length, p.width, p.thickness};
            sp.world = a.placement * p.placement;
            const domain::Material* m = model.findMaterial(p.material);
            sp.color = m ? m->color : kFallbackColor;
            valid_.push_back(validSize(sp.size));
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

std::optional<std::size_t> Scene::pick(const geometry::Ray3& ray) const {
    std::optional<std::size_t> best;
    double bestT = 0;
    for (std::size_t i = 0; i < parts_.size(); ++i) {
        if (!isVisible(i)) continue;
        const auto hit = geometry::intersectRayOrientedBox(ray, parts_[i].size, parts_[i].world);
        if (hit && (!best || hit->t < bestT)) {
            best = i;
            bestT = hit->t;
        }
    }
    return best;
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
        const auto mesh = geometry::transformed(geometry::makeBox(p.size), p.world);
        const auto base = static_cast<std::uint32_t>(out.vertices.size());
        for (std::size_t v = 0; v < mesh.positions.size(); ++v) {
            const Vec3 q = mesh.positions[v], nn = mesh.normals[v];
            out.vertices.push_back({f(q.x), f(q.y), f(q.z), f(nn.x), f(nn.y), f(nn.z), idx});
        }
        for (auto k : mesh.indices) out.indices.push_back(base + k);
        for (const auto& e : geometry::transformed(geometry::makeBoxEdges(p.size), p.world)) {
            out.lines.push_back({f(e.a.x), f(e.a.y), f(e.a.z), idx});
            out.lines.push_back({f(e.b.x), f(e.b.y), f(e.b.z), idx});
        }
    }
    return out;
}

} // namespace fos::viewport
