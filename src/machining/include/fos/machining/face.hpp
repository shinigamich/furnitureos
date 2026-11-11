// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/part.hpp"
#include "fos/geometry/vec.hpp"

#include <array>

/// Geometry of the six machinable faces of a panel.
///
/// This header is the executable form of the face conventions documented in
/// `fos/domain/operation.hpp`; it does not define new conventions. All lengths in mm.
namespace fos::machining {

inline constexpr std::array<domain::Face, 6> kAllFaces{domain::Face::A,  domain::Face::B,  domain::Face::L1,
                                                       domain::Face::L2, domain::Face::W1, domain::Face::W2};

/// True for the four narrow faces L1, L2, W1, W2.
bool isEdgeFace(domain::Face f) noexcept;
/// A<->B, L1<->L2, W1<->W2.
domain::Face oppositeFace(domain::Face f) noexcept;

/// Frame of a face. `origin` is the point with face coordinates (u, v) = (0, 0) on the
/// face surface, `uAxis`/`vAxis` are the unit directions of increasing u / v and
/// `toolDir` is the unit direction in which a tool enters the material (inward normal).
/// The outward surface normal is `-toolDir`. Note: (uAxis, vAxis, toolDir) is not
/// necessarily right-handed; it is whatever operation.hpp prescribes.
struct FaceFrame {
    geometry::Vec3 origin;
    geometry::Vec3 uAxis;
    geometry::Vec3 vAxis;
    geometry::Vec3 toolDir;
};

struct Range {
    double min = 0;
    double max = 0;
    double length() const noexcept { return max - min; }
    /// min - eps <= x <= max + eps
    bool contains(double x, double eps = core::kLengthEps) const noexcept { return x >= min - eps && x <= max + eps; }
};

/// Valid (u, v) rectangle of a face and the amount of material behind it:
/// A/B: u in [0,L], v in [0,W], materialDepth = T;  L1/L2: u in [0,L], v in [0,T],
/// materialDepth = W;  W1/W2: u in [0,W], v in [0,T], materialDepth = L.
struct FaceExtents {
    Range u;
    Range v;
    double materialDepth = 0;
};

/// A point expressed relative to a face: (u, v) on the surface, `depth` into the
/// material (negative = outside the panel in front of the face).
struct FaceCoord {
    double u = 0;
    double v = 0;
    double depth = 0;
};

/// Face frame expressed in the PART frame.
FaceFrame faceFrame(const domain::Part& part, domain::Face face) noexcept;
/// Face frame expressed in the ASSEMBLY frame (through Part::placement).
FaceFrame faceFrameInAssembly(const domain::Part& part, domain::Face face) noexcept;
FaceExtents faceExtents(const domain::Part& part, domain::Face face) noexcept;

/// face (u, v, depth) -> point in the part frame.
geometry::Vec3 faceToPart(const domain::Part& part, domain::Face face, double u, double v, double depth = 0) noexcept;
/// point in the part frame -> face (u, v, depth). Exact inverse of faceToPart.
FaceCoord partToFace(const domain::Part& part, domain::Face face, geometry::Vec3 partPoint) noexcept;
/// face (u, v, depth) -> point in the owning assembly's frame.
geometry::Vec3 faceToAssembly(const domain::Part& part, domain::Face face, double u, double v,
                              double depth = 0) noexcept;
/// point in the assembly frame -> face (u, v, depth).
FaceCoord assemblyToFace(const domain::Part& part, domain::Face face, geometry::Vec3 assemblyPoint) noexcept;

/// Axis of a drilled hole: `start` is the hole centre on the face surface, `direction`
/// the unit drilling direction, `length` the drilled depth (the full material depth
/// behind the face for a through hole) and `radius` half the diameter.
struct HoleAxis {
    geometry::Vec3 start;
    geometry::Vec3 direction;
    double length = 0;
    double radius = 0;
};

/// Drilled depth of a hole: `hole.depth`, or the material depth behind the face when `through`.
double effectiveDepth(const domain::Part& part, const domain::Hole& hole) noexcept;
/// Hole axis in the part frame.
HoleAxis holeAxis(const domain::Part& part, const domain::Hole& hole) noexcept;
/// Hole axis in the assembly frame.
HoleAxis holeAxisInAssembly(const domain::Part& part, const domain::Hole& hole) noexcept;

} // namespace fos::machining
