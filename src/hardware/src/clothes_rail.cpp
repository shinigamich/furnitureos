// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/hardware/clothes_rail.hpp"

#include "fos/hardware/catalog.hpp"
#include "util.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace fos::hardware {
using namespace detail;
using domain::Face;
using domain::Part;
using geometry::Vec3;

namespace {

std::string nameOf(const domain::HardwareDefinition& d) { return d.name.empty() ? d.id.str() : d.name; }

core::Error violation(std::string message, const std::string& entity) {
    return core::Error{core::ErrorCode::ConstraintViolation, std::move(message), entity};
}

core::Result<std::map<std::string, double>> readKeys(const domain::HardwareDefinition& d,
                                                     std::initializer_list<const char*> keys) {
    std::map<std::string, double> v;
    for (const char* key : keys) {
        auto r = requireParam(d, key);
        if (!r) return r.error();
        v[key] = r.value();
    }
    return v;
}

core::Result<std::map<std::string, double>> readRail(const domain::HardwareDefinition& rail) {
    using namespace keys;
    return readKeys(rail, {kRailHeight, kRailThickness, kHookClearance, kSectionDepthMin, kHangingSpaceMin});
}

core::Result<std::map<std::string, double>> readSupport(const domain::HardwareDefinition& s) {
    using namespace keys;
    auto v = readKeys(s, {kBodyHeight, kBodyDepth, kScrewSpacing, kScrewDiameter, kScrewDepth});
    if (!v) return v;
    auto& m = v.value();
    if (m[kScrewSpacing] / 2 + m[kScrewDiameter] / 2 > m[kBodyHeight] / 2 + core::kLengthEps)
        return core::Error{core::ErrorCode::InvalidArgument,
                           "The screw holes of rail support '" + nameOf(s) + "' (" + num(m[kScrewSpacing]) +
                               " mm apart, " + num(m[kScrewDiameter]) + " mm diameter) lie outside its " +
                               num(m[kBodyHeight]) + " mm high body.",
                           s.id.str()};
    if (m[kScrewDiameter] > m[kBodyDepth] + core::kLengthEps)
        return core::Error{core::ErrorCode::InvalidArgument,
                           "The " + num(m[kScrewDiameter]) + " mm screw holes of rail support '" + nameOf(s) +
                               "' are wider than its " + num(m[kBodyDepth]) + " mm deep body.",
                           s.id.str()};
    return v;
}

/// Sets `out` to the face of `p` whose outward normal points along sign(dir) * x; false when
/// the panel is not vertical with its faces towards +-x.
bool faceTowards(const Part& p, double dir, Face& out) {
    const Vec3 n = outwardNormal(p, Face::A);
    if (!isParallel(n, {1, 0, 0})) return false;
    out = (n.x * dir > 0) ? Face::A : Face::B;
    return true;
}

} // namespace

core::Result<ClothesRailAxis> planClothesRail(const domain::HardwareDefinition& rail,
                                              const domain::HardwareDefinition& support,
                                              const ClothesRailSpace& space) {
    auto rv = readRail(rail);
    if (!rv) return rv.error();
    auto sv = readSupport(support);
    if (!sv) return sv.error();
    using namespace keys;
    auto& r = rv.value();
    auto& s = sv.value();
    const std::string what = "clothes rail '" + nameOf(rail) + "'";
    const double depth = space.yBack - space.yFront;
    if (depth < r[kSectionDepthMin] - core::kLengthEps)
        return violation("The section is only " + num(depth) + " mm deep; " + what + " needs at least " +
                             num(r[kSectionDepthMin]) + " mm (section_depth_min) of clear depth for coat hangers.",
                         rail.id.str());
    if (!std::isfinite(space.heightFromTop) || space.heightFromTop <= 0)
        return violation("The rail height below the section top must be a positive length, got " +
                             num(space.heightFromTop) + " mm.",
                         rail.id.str());
    const double minFromTop = std::max(s[kBodyHeight] / 2, r[kRailHeight] / 2 + r[kHookClearance]);
    if (space.heightFromTop < minFromTop - core::kLengthEps)
        return violation("The rail axis is only " + num(space.heightFromTop) + " mm below the section top; " + what +
                             " with support '" + nameOf(support) + "' needs at least " + num(minFromTop) +
                             " mm (half the support height " + num(s[kBodyHeight] / 2) + " mm, half the rail " +
                             num(r[kRailHeight] / 2) + " mm + hook clearance " + num(r[kHookClearance]) + " mm).",
                         rail.id.str());
    ClothesRailAxis axis;
    axis.y = (space.yFront + space.yBack) / 2;
    axis.z = space.zTop - space.heightFromTop;
    const double below = axis.z - space.zBottom;
    if (below < r[kHangingSpaceMin] - core::kLengthEps)
        return violation("The section is too low for " + what + ": only " + num(below) +
                             " mm remain below the rail axis (section " + num(space.zTop - space.zBottom) +
                             " mm high, rail " + num(space.heightFromTop) + " mm below its top), at least " +
                             num(r[kHangingSpaceMin]) + " mm (hanging_space_min) are needed for garments.",
                         rail.id.str());
    return axis;
}

core::Result<Placement> placeClothesRail(const Part& left, const Part& right, const domain::HardwareDefinition& rail,
                                         const domain::HardwareDefinition& support, const ClothesRailConfig& cfg) {
    if (auto r = checkInstanceId(cfg.railInstanceId); !r) return r.error();
    if (auto r = checkInstanceId(cfg.supportInstanceId); !r) return r.error();
    if (cfg.railInstanceId == cfg.supportInstanceId)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "The clothes rail and its supports need different instance ids, both are '" +
                                   cfg.railInstanceId.str() + "'.",
                               cfg.railInstanceId.str());
    for (const Part* p : {&left, &right})
        if (auto r = checkPart(*p); !r) return r.error();
    auto axis = planClothesRail(rail, support, cfg.space);
    if (!axis) return axis.error();
    auto sv = readSupport(support);
    if (!sv) return sv.error();
    using namespace keys;
    auto& s = sv.value();
    const std::string what = nameOf(support);

    Face lf = Face::A, rf = Face::A;
    for (const auto& [p, dir, face] : {std::tuple{&left, 1.0, &lf}, std::tuple{&right, -1.0, &rf}})
        if (!faceTowards(*p, dir, *face))
            return violation(quoted(*p) + " must stand vertically with its faces towards the left and right (x) to "
                             "carry rail support '" + what + "'.",
                             p->id.str());
    const double xl = extentAlong(left, {1, 0, 0}).max, xr = extentAlong(right, {1, 0, 0}).min;
    const double length = xr - xl;
    if (!(length > core::kLengthEps))
        return violation("The inner faces of " + quoted(left) + " (x = " + num(xl) + ") and " + quoted(right) +
                             " (x = " + num(xr) + ") do not face each other; no clothes rail fits between them.",
                         left.id.str());

    const ClothesRailAxis a = axis.value();
    OpWriter w(cfg.supportInstanceId);
    domain::HardwareInstance sup;
    sup.id = cfg.supportInstanceId;
    sup.definition = support.id;
    sup.quantity = 2;
    sup.parts = {left.id, right.id};
    sup.note = "Rail supports on " + left.name + " and " + right.name;
    for (const auto& [p, face, x, maxDepth] :
         {std::tuple{&left, lf, xl, cfg.maxScrewDepthLeft}, std::tuple{&right, rf, xr, cfg.maxScrewDepthRight}}) {
        const double depth = maxDepth > 0 ? std::min(maxDepth, s[kScrewDepth]) : s[kScrewDepth];
        const double m = cfg.fit.minRemainingMaterial;
        if (depth >= p->thickness - m - core::kLengthEps)
            return violation("The " + num(depth) + " mm deep screw holes of rail support '" + what + "' would leave " +
                                 num(p->thickness - depth) + " mm of the " + num(p->thickness) + " mm thick " +
                                 quoted(*p) + "; more than " + num(m) + " mm must remain.",
                             p->id.str());
        for (int k = 0; k < 2; ++k) {
            const Vec3 pt{x, a.y, a.z + (k == 0 ? 1.0 : -1.0) * s[kScrewSpacing] / 2};
            if (p == &left && k == 0) sup.position = pt;
            const auto c = machining::assemblyToFace(*p, face, pt);
            if (!circleOnFace(*p, face, c.u, c.v, s[kScrewDiameter] / 2))
                return violation("Rail support screw hole at (" + num(c.u) + ", " + num(c.v) + ") on face " +
                                     domain::toString(face) + " of " + quoted(*p) + " would not lie on the panel (" +
                                     num(p->length) + " x " + num(p->width) + " mm).",
                                 p->id.str());
            w.add(*p, domain::Hole{face, c.u, c.v, s[kScrewDiameter], depth, false},
                  what + ": " + (p == &left ? "left" : "right") + " support screw " + std::to_string(k + 1) + " of 2");
        }
    }
    domain::HardwareInstance r;
    r.id = cfg.railInstanceId;
    r.definition = rail.id;
    r.quantity = 1;
    r.parts = {left.id, right.id};
    r.position = {xl, a.y, a.z};
    r.length = length;
    r.note = "Clothes rail " + num(length) + " mm (cut to the clear width); axis at z = " + num(a.z) + " mm, " +
             num(cfg.space.heightFromTop) + " mm below the section top";
    Placement out;
    out.instances = {r, sup};
    out.operations = w.take();
    return out;
}

} // namespace fos::hardware
