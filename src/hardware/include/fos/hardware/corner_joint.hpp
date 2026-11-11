// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/hardware/compatibility.hpp"
#include "fos/hardware/placement.hpp"
#include "fos/machining/face.hpp"

#include <optional>
#include <vector>

/// Butt (corner / T) joints between two panels of ONE assembly: an edge face of the
/// "edge part" lies flat against face A or B of the "face part". Lengths in mm.
namespace fos::hardware {

/// A detected butt contact.
struct ButtJoint {
    domain::Face faceOnFacePart = domain::Face::A;  ///< Contact face of the face part (A or B).
    domain::Face edgeOnEdgePart = domain::Face::L1; ///< Butting edge face of the edge part.
    /// Part of the butting edge that is in contact, as a range of the edge face's u
    /// coordinate (see operation.hpp: u = local x for L1/L2, local y for W1/W2).
    machining::Range along;
};

/// Detects the contact from the two placements (both parts must be placed in the same
/// assembly frame). Succeeds only when an edge face of `edgePart` is anti-parallel to and
/// coplanar (within `tolerance`) with face A or B of `facePart`, the parts are
/// axis-aligned to each other, the contact has positive length, and the WHOLE thickness
/// of the edge lies on the face. Otherwise returns a ConstraintViolation error that says
/// what is wrong; nothing is guessed.
core::Result<ButtJoint> detectButtJoint(const domain::Part& facePart, const domain::Part& edgePart,
                                        double tolerance = 0.1);

/// Fastener positions along a joint of length `jointLength`, measured from the joint start.
///  * count == 0 (automatic): first/last fastener `minEndOffset` from the joint ends,
///    n = max(2, ceil((jointLength - 2*minEndOffset) / maxSpacing) + 1) evenly spaced;
///    a joint of exactly 2*minEndOffset gets a single centred fastener; a shorter joint
///    is an error.
///  * count == 1: a single fastener in the middle.   * count >= 2: evenly spaced between
///    the end offsets (error when the joint is not longer than 2*minEndOffset).
core::Result<std::vector<double>> fastenerPositions(double jointLength, double minEndOffset, double maxSpacing,
                                                    int count = 0);

struct CornerJointConfig {
    domain::HardwareInstanceId instanceId; ///< Required; id of the produced instance.
    double minEndOffset = 50.0;            ///< First/last fastener from the ends of the joint.
    double maxSpacing = 250.0;             ///< Largest allowed fastener spacing (automatic count).
    int count = 0;                         ///< 0 = automatic, otherwise the exact number.
    double contactTolerance = 0.1;         ///< Allowed gap/overlap between the two surfaces.
    /// Dowels only: override the definition's face_depth / edge_depth.
    std::optional<double> dowelFaceDepth;
    std::optional<double> dowelEdgeDepth;
    FitOptions fit;
};

/// Connects the edge of `edgePart` to the face of `facePart` with N fasteners of kind
/// Confirmat or Dowel (taken from `fastener.kind`; other kinds are refused).
///
/// Produced holes per fastener i (operation ids `<instance>/op.(2i-1)` and `/op.(2i)`):
///  * Confirmat: a THROUGH hole (through_diameter) in `facePart`, entered from the face
///    OPPOSITE the contact face (the outside of the corner, where the screw head sits),
///    and a pilot hole (pilot_diameter, depth = length - facePart.thickness +
///    pilot_extra_depth) in the butting edge of `edgePart`.
///  * Dowel: a blind hole (diameter, face_depth) in the CONTACT face of `facePart` and a
///    hole (diameter, edge_depth) in the butting edge of `edgePart`.
/// Both holes of a fastener are coaxial in the assembly frame and the edge hole is
/// centred in the thickness of `edgePart`.
///
/// Result: one HardwareInstance (quantity N, parts {facePart, edgePart}, position = first
/// fastener on the contact plane, assembly frame) and operations for facePart, edgePart
/// (in that order). Errors: no butt contact, joint too short, fastener unsuitable for
/// the panels (see compatibility.hpp), hole would leave the face part.
core::Result<Placement> connectCorner(const domain::Part& facePart, const domain::Part& edgePart,
                                      const domain::HardwareDefinition& fastener, const CornerJointConfig& config);

} // namespace fos::hardware
