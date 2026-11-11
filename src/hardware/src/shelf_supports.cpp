// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/hardware/shelf_supports.hpp"

#include "fos/hardware/catalog.hpp"
#include "util.hpp"

#include <optional>

namespace fos::hardware {
using namespace detail;
using domain::Face;
using domain::Part;
using geometry::Vec3;

namespace {
struct Facing {
    Face shelfEdge;
    Face sideFace;
    double gap;
};

/// The (shelf edge, side face) pair that face each other across a small clearance.
std::optional<Facing> findFacing(const Part& shelf, const Part& side, const ShelfSupportConfig& cfg) {
    for (Face e : {Face::L1, Face::L2, Face::W1, Face::W2}) {
        const auto fe = machining::faceFrameInAssembly(shelf, e);
        for (Face f : {Face::A, Face::B}) {
            const auto fs = machining::faceFrameInAssembly(side, f);
            if (dot(fe.toolDir, fs.toolDir) > -1 + kAngleEps) continue;
            const double gap = dot(fe.origin - fs.origin, -fs.toolDir);
            if (gap >= -cfg.tolerance && gap <= cfg.maxGap + core::kLengthEps) return Facing{e, f, gap};
        }
    }
    return std::nullopt;
}
} // namespace

core::Result<Placement> placeShelfSupports(const Part& shelf, const Part& firstSide, const Part& secondSide,
                                           const domain::HardwareDefinition& pin, const ShelfSupportConfig& cfg) {
    if (auto r = checkInstanceId(cfg.instanceId); !r) return r.error();
    for (const Part* p : {&shelf, &firstSide, &secondSide})
        if (auto r = checkPart(*p); !r) return r.error();
    auto fail = [&](std::string m, const Part& p) {
        return core::makeError(core::ErrorCode::ConstraintViolation, std::move(m), p.id.str());
    };

    // Underside of the shelf: the face whose outward normal is straight down.
    std::optional<double> underside;
    for (Face f : {Face::A, Face::B})
        if (outwardNormal(shelf, f).z < -1 + kAngleEps) underside = machining::faceFrameInAssembly(shelf, f).origin.z;
    if (!underside)
        return fail("Shelf " + quoted(shelf) + " is not horizontal (neither face A nor B points straight down); "
                    "shelf supports can only be placed under a horizontal shelf.", shelf);

    const Part* sides[2] = {&firstSide, &secondSide};
    Facing facing[2]{};
    for (int i = 0; i < 2; ++i) {
        if (auto r = firstError(checkShelfPinFit(pin, *sides[i], cfg.fit)); !r) return r.error();
        const auto f = findFacing(shelf, *sides[i], cfg);
        if (!f)
            return fail("Shelf " + quoted(shelf) + " does not sit against " + quoted(*sides[i]) +
                            ": no edge of the shelf faces face A or B of that panel within a clearance of 0.." +
                            num(cfg.maxGap) + " mm.", *sides[i]);
        facing[i] = *f;
    }
    if (facing[0].shelfEdge == facing[1].shelfEdge)
        return fail(quoted(firstSide) + " and " + quoted(secondSide) + " both face edge " +
                        domain::toString(facing[0].shelfEdge) + " of shelf " + quoted(shelf) +
                        "; the shelf must lie between the two sides.", shelf);

    const double d = pin.params.at(keys::kHoleDiameter), depth = pin.params.at(keys::kHoleDepth);
    const double z = *underside - d / 2;
    const std::string what = pin.name.empty() ? pin.id.str() : pin.name;

    OpWriter w(cfg.instanceId);
    domain::HardwareInstance inst;
    inst.id = cfg.instanceId;
    inst.definition = pin.id;
    inst.quantity = 4;
    inst.parts = {firstSide.id, secondSide.id, shelf.id};
    inst.note = "Supports " + shelf.name;

    for (int i = 0; i < 2; ++i) {
        const Part& side = *sides[i];
        const auto ext = machining::faceExtents(shelf, facing[i].shelfEdge);
        const double len = ext.u.max;
        if (cfg.setbackFront < 0 || cfg.setbackBack < 0 || cfg.setbackFront + cfg.setbackBack > len + core::kLengthEps)
            return fail("Shelf-pin setbacks of " + num(cfg.setbackFront) + " mm (front) and " + num(cfg.setbackBack) +
                            " mm (back) do not fit the " + num(len) + " mm long edge " +
                            domain::toString(facing[i].shelfEdge) + " of shelf " + quoted(shelf) + ".", shelf);
        // Which end of the shelf edge is the front (smaller assembly Y, then smaller X)?
        const Vec3 p0 = machining::faceToAssembly(shelf, facing[i].shelfEdge, 0, 0);
        const Vec3 p1 = machining::faceToAssembly(shelf, facing[i].shelfEdge, len, 0);
        const bool startIsFront =
            std::fabs(p0.y - p1.y) > core::kLengthEps ? p0.y < p1.y : p0.x < p1.x;
        const double uFront = startIsFront ? cfg.setbackFront : len - cfg.setbackFront;
        const double uBack = startIsFront ? len - cfg.setbackBack : cfg.setbackBack;
        const Vec3 n = outwardNormal(side, facing[i].sideFace);
        const char* label[2] = {"front", "back"};
        int k = 0;
        for (const double u : {uFront, uBack}) {
            Vec3 p = machining::faceToAssembly(shelf, facing[i].shelfEdge, u, 0);
            p = p - n * facing[i].gap; // onto the side's inner face
            p.z = z;
            const auto fc = machining::assemblyToFace(side, facing[i].sideFace, p);
            if (!circleOnFace(side, facing[i].sideFace, fc.u, fc.v, d / 2))
                return fail("The " + std::string(label[k]) + " shelf-pin hole for " + quoted(shelf) + " at (" +
                                num(fc.u) + ", " + num(fc.v) + ") on face " + domain::toString(facing[i].sideFace) +
                                " of " + quoted(side) + " would not lie completely on that panel (" +
                                num(side.length) + " x " + num(side.width) + " mm).", side);
            if (i == 0 && k == 0) inst.position = p;
            w.add(side, domain::Hole{facing[i].sideFace, fc.u, fc.v, d, depth, false},
                  what + ": " + label[k] + " pin under " + shelf.name);
            ++k;
        }
    }
    Placement out;
    out.instances = {inst};
    out.operations = w.take();
    return out;
}

} // namespace fos::hardware
