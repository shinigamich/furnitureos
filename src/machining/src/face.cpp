// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/machining/face.hpp"

namespace fos::machining {
using domain::Face;
using domain::Part;
using geometry::Vec3;

bool isEdgeFace(Face f) noexcept { return f != Face::A && f != Face::B; }

Face oppositeFace(Face f) noexcept {
    switch (f) {
    case Face::A: return Face::B;
    case Face::B: return Face::A;
    case Face::L1: return Face::L2;
    case Face::L2: return Face::L1;
    case Face::W1: return Face::W2;
    case Face::W2: return Face::W1;
    }
    return f;
}

FaceFrame faceFrame(const Part& p, Face f) noexcept {
    switch (f) {
    case Face::A: return {{0, 0, p.thickness}, {1, 0, 0}, {0, 1, 0}, {0, 0, -1}};
    case Face::B: return {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    case Face::L1: return {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}};
    case Face::L2: return {{0, p.width, 0}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}};
    case Face::W1: return {{0, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}};
    case Face::W2: return {{p.length, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-1, 0, 0}};
    }
    return {};
}

FaceFrame faceFrameInAssembly(const Part& p, Face f) noexcept {
    const FaceFrame l = faceFrame(p, f);
    const auto& t = p.placement;
    return {t.applyPoint(l.origin), t.applyVector(l.uAxis), t.applyVector(l.vAxis), t.applyVector(l.toolDir)};
}

FaceExtents faceExtents(const Part& p, Face f) noexcept {
    switch (f) {
    case Face::A:
    case Face::B: return {{0, p.length}, {0, p.width}, p.thickness};
    case Face::L1:
    case Face::L2: return {{0, p.length}, {0, p.thickness}, p.width};
    case Face::W1:
    case Face::W2: return {{0, p.width}, {0, p.thickness}, p.length};
    }
    return {};
}

Vec3 faceToPart(const Part& p, Face f, double u, double v, double depth) noexcept {
    const FaceFrame fr = faceFrame(p, f);
    return fr.origin + fr.uAxis * u + fr.vAxis * v + fr.toolDir * depth;
}

FaceCoord partToFace(const Part& p, Face f, Vec3 pt) noexcept {
    const FaceFrame fr = faceFrame(p, f);
    const Vec3 d = pt - fr.origin;
    return {dot(d, fr.uAxis), dot(d, fr.vAxis), dot(d, fr.toolDir)};
}

Vec3 faceToAssembly(const Part& p, Face f, double u, double v, double depth) noexcept {
    return p.placement.applyPoint(faceToPart(p, f, u, v, depth));
}

FaceCoord assemblyToFace(const Part& p, Face f, Vec3 pt) noexcept {
    return partToFace(p, f, p.placement.inverse().applyPoint(pt));
}

double effectiveDepth(const Part& p, const domain::Hole& h) noexcept {
    return h.through ? faceExtents(p, h.face).materialDepth : h.depth;
}

HoleAxis holeAxis(const Part& p, const domain::Hole& h) noexcept {
    return {faceToPart(p, h.face, h.u, h.v), faceFrame(p, h.face).toolDir, effectiveDepth(p, h), h.diameter / 2};
}

HoleAxis holeAxisInAssembly(const Part& p, const domain::Hole& h) noexcept {
    const HoleAxis a = holeAxis(p, h);
    return {p.placement.applyPoint(a.start), p.placement.applyVector(a.direction), a.length, a.radius};
}

} // namespace fos::machining
