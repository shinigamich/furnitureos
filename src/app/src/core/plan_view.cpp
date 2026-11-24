// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/plan_view.hpp"

#include "fos/app/session.hpp"
#include "fos/geometry/contour.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace fos::app {

namespace {

constexpr double kOverlapEps = 1e-6;  // Y ranges must share more than this to be neighbours.
constexpr double kTouchGap = 0.05;    // Gaps up to this are "touching" and not listed.
constexpr double kChordTolerance = 0.5;

geometry::Vec2 xy(geometry::Vec3 p) { return {p.x, p.y}; }

geometry::Rect2 xyRect(const geometry::Aabb& box) {
    if (box.empty()) return {};
    return {{box.min.x, box.min.y}, {box.max.x, box.max.y}};
}

/// Projection of one part (world = assembly placement * part placement) to the project XY plane.
geometry::Polygon2 partOutline(const domain::Part& part, const geometry::Transform& world, bool& shaped) {
    const bool flat = std::fabs(std::fabs(world.zAxis.z) - 1.0) <= 1e-9;
    if (!flat) return geometry::makeRectPolygon(xyRect(part.localBounds().transformed(world)));
    geometry::Polygon2 local;
    if (part.shape && !part.shape->outer.vertices.empty()) {
        local = geometry::tessellate(part.shape->outer, kChordTolerance);
        shaped = true;
    } else {
        local = geometry::makeRectPolygon(geometry::Rect2::fromSize(part.length, part.width));
    }
    geometry::Polygon2 out;
    out.vertices.reserve(local.vertices.size());
    for (const auto& v : local.vertices) out.vertices.push_back(xy(world.applyPoint({v.x, v.y, 0})));
    return out;
}

bool yOverlap(const geometry::Rect2& a, const geometry::Rect2& b) {
    return std::min(a.max.y, b.max.y) - std::max(a.min.y, b.min.y) > kOverlapEps;
}

double yMiddle(const geometry::Rect2& a, const geometry::Rect2& b) {
    return 0.5 * (std::max(a.min.y, b.min.y) + std::min(a.max.y, b.max.y));
}

double roundTo(double v, double grid) { return grid > 0 ? std::round(v / grid) * grid : v; }

/// Best snap of one axis: smallest |target - edge| <= tolerance over (edges x targets).
struct AxisSnap {
    double shift = 0;
    double target = 0;
    bool found = false;
};

AxisSnap snapAxis(double movingMin, double movingMax, const std::vector<double>& targets, double tolerance) {
    AxisSnap best;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (double t : targets) {
        for (double edge : {movingMin, movingMax}) {
            const double d = std::fabs(t - edge);
            if (d <= tolerance && d < bestDistance - 1e-12) {
                bestDistance = d;
                best = {t - edge, t, true};
            }
        }
    }
    return best;
}

QString number(double v) {
    const double r = std::round(v * 10.0) / 10.0;
    if (std::fabs(r - std::round(r)) < 1e-9) return QString::number(static_cast<long long>(std::llround(r)));
    return QString::number(r, 'f', 1);
}

} // namespace

std::vector<PlanFootprint> planFootprints(const project::Project& project, const project::Resolved& resolved) {
    std::vector<PlanFootprint> out;
    for (const auto& item : project.items) {
        const domain::Assembly* assembly = resolved.model.findAssembly(domain::AssemblyId(item.id()));
        if (assembly == nullptr) continue;
        PlanFootprint f;
        f.itemId = item.id();
        f.name = item.name();
        f.bounds = xyRect(assembly->bounds().transformed(assembly->placement));
        for (const auto& part : assembly->parts) {
            geometry::Polygon2 poly = partOutline(part, assembly->placement * part.placement, f.shaped);
            if (poly.size() >= 3) f.outlines.push_back(std::move(poly));
        }
        if (f.outlines.empty() && !f.bounds.empty()) f.outlines.push_back(geometry::makeRectPolygon(f.bounds));
        out.push_back(std::move(f));
    }
    return out;
}

std::vector<PlanFootprint> planFootprints(const Session& session) {
    const project::Resolved* resolved = session.resolved();
    if (resolved == nullptr) return {};
    return planFootprints(session.project(), *resolved);
}

geometry::Rect2 planExtents(const std::vector<PlanFootprint>& footprints) {
    geometry::Rect2 r;
    for (const auto& f : footprints) r.expand(f.bounds);
    return r;
}

core::Result<void> checkPlanRoom(const PlanRoom& room) {
    auto bad = [](const std::string& what, double v) {
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "The room " + what + " " + std::to_string(v) + " mm is not valid.", "room");
    };
    if (!std::isfinite(room.width) || room.width <= 0) return bad("width", room.width);
    if (!std::isfinite(room.depth) || room.depth <= 0) return bad("depth", room.depth);
    if (!std::isfinite(room.wallThickness) || room.wallThickness < 0) return bad("wall thickness", room.wallThickness);
    if (!std::isfinite(room.x)) return bad("x position", room.x);
    if (!std::isfinite(room.y)) return bad("y position", room.y);
    return {};
}

std::vector<PlanGap> planGapsX(const std::vector<PlanFootprint>& footprints, const std::optional<PlanRoom>& room) {
    const bool hasRoom = room && checkPlanRoom(*room).ok();
    const geometry::Rect2 inner = hasRoom ? room->inner() : geometry::Rect2{};
    std::vector<PlanGap> gaps;
    for (std::size_t i = 0; i < footprints.size(); ++i) {
        const auto& a = footprints[i].bounds;
        if (a.empty()) continue;
        const PlanFootprint* right = nullptr;
        bool hasLeft = false;
        for (std::size_t j = 0; j < footprints.size(); ++j) {
            if (j == i) continue;
            const auto& b = footprints[j].bounds;
            if (b.empty() || !yOverlap(a, b)) continue;
            if (b.min.x >= a.max.x - kOverlapEps && (right == nullptr || b.min.x < right->bounds.min.x))
                right = &footprints[j];
            if (b.max.x <= a.min.x + kOverlapEps) hasLeft = true;
        }
        if (right != nullptr) {
            const double g = right->bounds.min.x - a.max.x;
            if (g > kTouchGap)
                gaps.push_back({footprints[i].itemId, right->itemId, a.max.x, right->bounds.min.x,
                                yMiddle(a, right->bounds)});
        }
        if (!hasRoom || !yOverlap(a, inner)) continue;
        const bool insideX = a.min.x >= inner.min.x - kOverlapEps && a.max.x <= inner.max.x + kOverlapEps;
        if (!insideX) continue;
        if (right == nullptr && inner.max.x - a.max.x > kTouchGap)
            gaps.push_back({footprints[i].itemId, {}, a.max.x, inner.max.x, yMiddle(a, inner)});
        if (!hasLeft && a.min.x - inner.min.x > kTouchGap)
            gaps.push_back({{}, footprints[i].itemId, inner.min.x, a.min.x, yMiddle(a, inner)});
    }
    std::sort(gaps.begin(), gaps.end(), [](const PlanGap& p, const PlanGap& q) {
        return std::tie(p.fromX, p.y, p.leftId, p.rightId) < std::tie(q.fromX, q.y, q.leftId, q.rightId);
    });
    return gaps;
}

PlanSnap snapPlanMove(const std::vector<PlanFootprint>& footprints, const std::string& movingId,
                      geometry::Vec2 delta, double tolerance, const std::optional<PlanRoom>& room, double grid) {
    PlanSnap out;
    out.delta = {roundTo(delta.x, grid), roundTo(delta.y, grid)};
    const auto moving = std::find_if(footprints.begin(), footprints.end(),
                                     [&](const PlanFootprint& f) { return f.itemId == movingId; });
    if (moving == footprints.end() || moving->bounds.empty() || !(tolerance >= 0)) return out;

    std::vector<double> xs, ys;
    for (const auto& f : footprints) {
        if (f.itemId == movingId || f.bounds.empty()) continue;
        xs.insert(xs.end(), {f.bounds.min.x, f.bounds.max.x});
        ys.insert(ys.end(), {f.bounds.min.y, f.bounds.max.y});
    }
    if (room && checkPlanRoom(*room).ok()) {
        const auto inner = room->inner();
        xs.insert(xs.end(), {inner.min.x, inner.max.x});
        ys.insert(ys.end(), {inner.min.y, inner.max.y});
    }
    const auto& b = moving->bounds;
    const AxisSnap sx = snapAxis(b.min.x + delta.x, b.max.x + delta.x, xs, tolerance);
    const AxisSnap sy = snapAxis(b.min.y + delta.y, b.max.y + delta.y, ys, tolerance);
    if (sx.found) {
        out.delta.x = delta.x + sx.shift;
        out.guideX = sx.target;
    }
    if (sy.found) {
        out.delta.y = delta.y + sy.shift;
        out.guideY = sy.target;
    }
    return out;
}

geometry::Transform movedPlacement(const geometry::Transform& placement, geometry::Vec2 delta) {
    return geometry::Transform::translation({delta.x, delta.y, 0}) * placement;
}

geometry::Transform rotatedPlacement(const geometry::Transform& placement, geometry::Vec2 pivot) {
    // Exact +90 degrees about Z: x -> y, y -> -x.
    const geometry::Transform rot{{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}, {}};
    return geometry::Transform::translation({pivot.x, pivot.y, 0}) * rot *
           geometry::Transform::translation({-pivot.x, -pivot.y, 0}) * placement;
}

core::Result<project::Command> planMoveCommand(const project::Project& project, const std::string& itemId,
                                               geometry::Vec2 delta) {
    const project::Item* item = project.findItem(itemId);
    if (item == nullptr)
        return core::makeError(core::ErrorCode::NotFound, "Item '" + itemId + "' does not exist.", itemId);
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "The move of '" + item->name() + "' is not a finite distance.", itemId);
    return project::commands::setPlacement(itemId, movedPlacement(item->placement(), delta));
}

core::Result<project::Command> planRotateCommand(const project::Project& project, const std::string& itemId,
                                                 geometry::Vec2 pivot) {
    const project::Item* item = project.findItem(itemId);
    if (item == nullptr)
        return core::makeError(core::ErrorCode::NotFound, "Item '" + itemId + "' does not exist.", itemId);
    if (!std::isfinite(pivot.x) || !std::isfinite(pivot.y))
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "The rotation centre of '" + item->name() + "' is not a finite point.", itemId);
    return project::commands::setPlacement(itemId, rotatedPlacement(item->placement(), pivot));
}

core::Result<void> applyPlanMove(Session& session, const std::string& itemId, geometry::Vec2 delta) {
    auto command = planMoveCommand(session.project(), itemId, delta);
    if (!command) return command.error();
    if (delta.x == 0 && delta.y == 0) return {};
    return session.execute(command.value());
}

core::Result<void> applyPlanRotate(Session& session, const std::string& itemId) {
    const auto footprints = planFootprints(session);
    const auto it = std::find_if(footprints.begin(), footprints.end(),
                                 [&](const PlanFootprint& f) { return f.itemId == itemId; });
    if (it == footprints.end() || it->bounds.empty())
        return core::makeError(core::ErrorCode::NotFound,
                               "Item '" + itemId + "' has no generated footprint to rotate.", itemId);
    auto command = planRotateCommand(session.project(), itemId, it->bounds.center());
    if (!command) return command.error();
    return session.execute(command.value());
}

std::string planItemAt(const std::vector<PlanFootprint>& footprints, geometry::Vec2 point, double tolerance) {
    for (auto it = footprints.rbegin(); it != footprints.rend(); ++it)
        for (const auto& poly : it->outlines)
            if (geometry::locatePoint(poly, point) != geometry::PointLocation::Outside) return it->itemId;
    for (auto it = footprints.rbegin(); it != footprints.rend(); ++it)
        if (!it->bounds.empty() && it->bounds.contains(point, std::max(0.0, tolerance))) return it->itemId;
    return {};
}

QString planSizeText(double sizeX, double sizeY) {
    return QCoreApplication::translate("fos::app", "%1 × %2 mm", "plan: overall X × Y size of an item")
        .arg(number(sizeX), number(sizeY));
}

QString planLengthText(double length) {
    return QCoreApplication::translate("fos::app", "%1 mm", "plan: a gap or length").arg(number(length));
}

} // namespace fos::app
