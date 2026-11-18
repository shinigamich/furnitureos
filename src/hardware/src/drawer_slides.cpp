// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/hardware/drawer_slides.hpp"

#include "fos/hardware/catalog.hpp"
#include "util.hpp"

#include <cmath>

namespace fos::hardware {
namespace {
using namespace detail;
using domain::Face;
using geometry::Vec3;

core::Error bad(const std::string& message, const std::string& entity) {
    return core::makeError(core::ErrorCode::ConstraintViolation, message, entity);
}

std::string defLabel(const domain::HardwareDefinition& d) { return "'" + d.id.str() + "' (" + d.name + ")"; }

core::Result<void> checkKind(const domain::HardwareDefinition& d) {
    if (d.kind != domain::HardwareKind::DrawerSlide)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Hardware definition " + defLabel(d) + " is of kind " + domain::toString(d.kind) +
                                   " but a drawer slide (kind DrawerSlide) is required.",
                               d.id.str());
    return {};
}

/// Positions first, first + pitch, ... <= limit.
std::vector<double> rowPositions(double first, double pitch, double limit) {
    std::vector<double> out;
    for (double p = first; p <= limit + core::kLengthEps && out.size() < 1000; p += pitch) out.push_back(p);
    return out;
}

/// The face (A/B) of `part` whose outward normal is `n`; nullopt when none.
std::optional<Face> faceWithNormal(const domain::Part& part, Vec3 n, bool edges) {
    const Face faces[] = {Face::A, Face::B, Face::L1, Face::L2, Face::W1, Face::W2};
    for (const Face f : faces) {
        if (edges != machining::isEdgeFace(f)) continue;
        if (dot(outwardNormal(part, f), n) > 1 - kAngleEps) return f;
    }
    return std::nullopt;
}

} // namespace

core::Result<bool> isUndermountSlide(const domain::HardwareDefinition& slide) {
    if (auto k = checkKind(slide); !k) return k.error();
    auto u = requireParam(slide, keys::kUndermount, true);
    if (!u) return u.error();
    if (u.value() != 0.0 && u.value() != 1.0)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Parameter 'undermount' of drawer slide " + defLabel(slide) + " is " + num(u.value()) +
                                   "; it must be 0 (side-mounted) or 1 (undermount).",
                               slide.id.str());
    return u.value() == 1.0;
}

core::Result<std::vector<double>> slideNominalLengths(const domain::HardwareDefinition& slide) {
    if (auto k = checkKind(slide); !k) return k.error();
    auto lo = requireParam(slide, keys::kLengthMin), hi = requireParam(slide, keys::kLengthMax),
         step = requireParam(slide, keys::kLengthStep);
    if (!lo) return lo.error();
    if (!hi) return hi.error();
    if (!step) return step.error();
    if (hi.value() < lo.value() || (hi.value() - lo.value()) / step.value() > 1000)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "Drawer slide " + defLabel(slide) + " has length_min " + num(lo.value()) +
                                   " mm, length_max " + num(hi.value()) + " mm and length_step " + num(step.value()) +
                                   " mm; length_max must not be below length_min and the range must hold at most "
                                   "1000 steps.",
                               slide.id.str());
    return rowPositions(lo.value(), step.value(), hi.value());
}

core::Result<double> selectSlideLength(const domain::HardwareDefinition& slide, double availableDepth) {
    auto lengths = slideNominalLengths(slide);
    if (!lengths) return lengths.error();
    auto allowance = requireParam(slide, keys::kDepthAllowance, true);
    if (!allowance) return allowance.error();
    double best = 0;
    for (const double l : lengths.value())
        if (l + allowance.value() <= availableDepth + core::kLengthEps) best = l;
    if (best > 0) return best;
    const double shortest = lengths.value().front();
    return bad("The usable depth of " + num(availableDepth) + " mm is too shallow for the shortest drawer slide " +
                   defLabel(slide) + ": a " + num(shortest) + " mm slide needs " + num(shortest + allowance.value()) +
                   " mm.",
               slide.id.str());
}

core::Result<double> drawerBoxOuterWidth(const domain::HardwareDefinition& slide, double openingWidth,
                                         double sideThickness) {
    auto under = isUndermountSlide(slide);
    if (!under) return under.error();
    double width = 0;
    if (under.value()) {
        auto deduction = requireParam(slide, keys::kInnerWidthDeduction, true);
        auto maxSide = requireParam(slide, keys::kMaxSideThickness);
        if (!deduction) return deduction.error();
        if (!maxSide) return maxSide.error();
        if (sideThickness > maxSide.value() + core::kLengthEps)
            return bad("Drawer box sides of " + num(sideThickness) + " mm are too thick for the undermount runner " +
                           defLabel(slide) + " (at most " + num(maxSide.value()) +
                           " mm); assign a thinner drawer material.",
                       slide.id.str());
        width = openingWidth - deduction.value() + 2 * sideThickness;
    } else {
        auto clearance = requireParam(slide, keys::kSideClearance, true);
        if (!clearance) return clearance.error();
        width = openingWidth - 2 * clearance.value();
    }
    if (!(width > 2 * sideThickness + core::kLengthEps))
        return bad("An opening of " + num(openingWidth) + " mm leaves a drawer box of " + num(width) +
                       " mm outer width with drawer slide " + defLabel(slide) + "; that is not wider than its two " +
                       num(sideThickness) + " mm sides.",
                   slide.id.str());
    return width;
}

core::Result<double> drawerBoxLength(const domain::HardwareDefinition& slide, double slideLength) {
    if (auto k = checkKind(slide); !k) return k.error();
    auto d = requireParam(slide, keys::kBoxLengthDeduction, true);
    if (!d) return d.error();
    if (!(slideLength - d.value() > 0))
        return bad("A slide length of " + num(slideLength) + " mm leaves no drawer box with " + defLabel(slide) + ".",
                   slide.id.str());
    return slideLength - d.value();
}

core::Result<Placement> placeDrawerSlides(const domain::Part& drawerSide, const domain::Part& cabinetSide,
                                          const domain::HardwareDefinition& slide, const DrawerSlideConfig& config) {
    if (auto r = checkInstanceId(config.instanceId); !r) return r.error();
    if (auto r = checkPart(drawerSide); !r) return r.error();
    if (auto r = checkPart(cabinetSide); !r) return r.error();
    auto under = isUndermountSlide(slide);
    if (!under) return under.error();
    auto lengths = slideNominalLengths(slide);
    if (!lengths) return lengths.error();
    const double L = config.slideLength;
    bool nominal = false;
    for (const double l : lengths.value()) nominal = nominal || std::fabs(l - L) <= core::kLengthEps;
    if (!nominal)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "A slide length of " + num(L) + " mm is not a nominal length of " + defLabel(slide) +
                                   " (" + num(lengths.value().front()) + " to " + num(lengths.value().back()) +
                                   " mm in steps of " + num(slide.params.at(keys::kLengthStep)) + " mm).",
                               slide.id.str());

    const char* numKeys[] = {keys::kHoleLineHeight,     keys::kCabinetHoleFirst,  keys::kCabinetHolePitch,
                             keys::kDrawerHoleFirst,    keys::kDrawerHolePitch,   keys::kHoleEndMargin,
                             keys::kCabinetHoleDiameter, keys::kCabinetHoleDepth, keys::kDrawerHoleDiameter,
                             keys::kDrawerHoleDepth};
    std::map<std::string, double> prm;
    for (const char* k : numKeys) {
        auto v = requireParam(slide, k);
        if (!v) return v.error();
        prm[k] = v.value();
    }

    // Cabinet face: the large face of the cabinet side that looks at the drawer side.
    const Vec3 up{0, 0, 1};
    const Vec3 towards = centreInAssembly(drawerSide) - centreInAssembly(cabinetSide);
    std::optional<Face> cabFace;
    Vec3 n{};
    for (const Face f : {Face::A, Face::B}) {
        const Vec3 o = outwardNormal(cabinetSide, f);
        if (dot(o, towards) > 0) {
            cabFace = f;
            n = o;
        }
    }
    const bool horizontalNormal = cabFace && (isParallel(n, {1, 0, 0}) || isParallel(n, {0, 1, 0}));
    if (!horizontalNormal || !isParallel(outwardNormal(drawerSide, Face::A), n))
        return bad("Drawer slide between " + quoted(drawerSide) + " and " + quoted(cabinetSide) +
                       ": both must be vertical panels whose faces are parallel and normal to the assembly X or Y "
                       "axis.",
                   drawerSide.id.str());
    const Vec3 along = isParallel(n, {1, 0, 0}) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};

    const auto cabN = extentAlong(cabinetSide, n), drwN = extentAlong(drawerSide, n);
    const double gap = drwN.min - cabN.max;
    const double sideT = drawerSide.thickness;
    double expected = 0;
    if (under.value()) {
        auto deduction = requireParam(slide, keys::kInnerWidthDeduction, true);
        if (!deduction) return deduction.error();
        expected = (deduction.value() - 2 * sideT) / 2;
    } else {
        auto clearance = requireParam(slide, keys::kSideClearance, true);
        if (!clearance) return clearance.error();
        expected = clearance.value();
    }
    if (std::fabs(gap - expected) > config.gapTolerance)
        return bad("The gap between " + quoted(drawerSide) + " and " + quoted(cabinetSide) + " is " + num(gap) +
                       " mm but drawer slide " + defLabel(slide) + " needs " + num(expected) + " mm (± " +
                       num(config.gapTolerance) + ").",
                   drawerSide.id.str());

    const auto cabA = extentAlong(cabinetSide, along), drwA = extentAlong(drawerSide, along);
    const double front = drwA.min;
    if (front < cabA.min - core::kLengthEps || front + L > cabA.max + core::kLengthEps)
        return bad("A " + num(L) + " mm slide starting at the front of " + quoted(drawerSide) + " (" + num(front) +
                       ") runs outside " + quoted(cabinetSide) + " (" + num(cabA.min) + " to " + num(cabA.max) + ").",
                   cabinetSide.id.str());
    const double zb = extentAlong(drawerSide, up).min;
    const double zLine = zb + prm[keys::kHoleLineHeight];

    OpWriter ops(config.instanceId);
    const auto addRow = [&](const domain::Part& part, Face face, double alongN, double z, double first, double pitch,
                            double limit, double diameter, double depth, const char* what) -> core::Result<void> {
        const auto positions = rowPositions(first, pitch, limit);
        if (positions.empty())
            return bad("No " + std::string(what) + " hole of drawer slide " + defLabel(slide) + " fits on " +
                           quoted(part) + ": the first hole is " + num(first) + " mm from the front but only " +
                           num(limit) + " mm are available.",
                       part.id.str());
        const double material = machining::faceExtents(part, face).materialDepth;
        if (depth >= material - core::kLengthEps)
            return bad("The " + std::string(what) + " holes of drawer slide " + defLabel(slide) + " are " + num(depth) +
                           " mm deep but " + quoted(part) + " has only " + num(material) + " mm behind face " +
                           domain::toString(face) + ".",
                       part.id.str());
        for (const double s : positions) {
            const Vec3 p = along * (front + s) + n * alongN + up * z;
            // n and along are assembly axes (up to sign), so this has the intended coordinates.
            const auto fc = machining::assemblyToFace(part, face, p);
            if (!circleOnFace(part, face, fc.u, fc.v, diameter / 2))
                return bad("The " + std::string(what) + " hole of drawer slide " + defLabel(slide) + " at " + num(s) +
                               " mm from the slide front, " + num(z - zb) + " mm above the drawer underside, lies "
                               "outside face " + domain::toString(face) + " of " + quoted(part) + ".",
                           part.id.str());
            domain::Hole h;
            h.face = face;
            h.u = fc.u;
            h.v = fc.v;
            h.diameter = diameter;
            h.depth = depth;
            ops.add(part, h, std::string("drawer slide, ") + what);
        }
        return {};
    };

    const double margin = prm[keys::kHoleEndMargin];
    const double cabDepth = std::min(prm[keys::kCabinetHoleDepth], config.maxCabinetHoleDepth);
    if (auto r = addRow(cabinetSide, *cabFace, cabN.max, zLine, prm[keys::kCabinetHoleFirst],
                        prm[keys::kCabinetHolePitch], L - margin, prm[keys::kCabinetHoleDiameter], cabDepth,
                        "cabinet member");
        !r)
        return r.error();
    const double drawerLimit = std::min(L, drwA.max - front) - margin;
    if (under.value()) {
        const auto lower = faceWithNormal(drawerSide, {0, 0, -1}, true);
        if (!lower)
            return bad(quoted(drawerSide) + " has no lower edge face pointing straight down; undermount runner " +
                           defLabel(slide) + " needs one for its fixing holes.",
                       drawerSide.id.str());
        if (auto r = addRow(drawerSide, *lower, (drwN.min + drwN.max) / 2, zb, prm[keys::kDrawerHoleFirst],
                            prm[keys::kDrawerHolePitch], drawerLimit, prm[keys::kDrawerHoleDiameter],
                            prm[keys::kDrawerHoleDepth], "drawer member");
            !r)
            return r.error();
    } else {
        const auto face = faceWithNormal(drawerSide, n * -1.0, false);
        if (!face) return bad(quoted(drawerSide) + " has no face towards " + quoted(cabinetSide) + ".", drawerSide.id.str());
        if (auto r = addRow(drawerSide, *face, drwN.min, zLine, prm[keys::kDrawerHoleFirst],
                            prm[keys::kDrawerHolePitch], drawerLimit, prm[keys::kDrawerHoleDiameter],
                            prm[keys::kDrawerHoleDepth], "drawer member");
            !r)
            return r.error();
    }

    domain::HardwareInstance inst;
    inst.id = config.instanceId;
    inst.definition = slide.id;
    inst.quantity = 1;
    inst.parts = {cabinetSide.id, drawerSide.id};
    inst.position = along * front + n * cabN.max + up * zLine;
    inst.note = "slide length " + num(L) + " mm";
    Placement out;
    out.instances.push_back(std::move(inst));
    out.operations = ops.take();
    return out;
}

} // namespace fos::hardware
