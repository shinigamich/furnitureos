// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Geometric self-checks over generated (or hand-edited) assemblies. generate() runs them on
/// every cabinet it produces (diagnostic furniture.geometry.interpenetration).
#include "fos/core/units.hpp"
#include "fos/domain/model.hpp"
#include "fos/geometry/aabb.hpp"

#include <string>
#include <vector>

namespace fos::furniture {

/// Two parts whose boxes share volume.
struct Interpenetration {
    std::size_t first = 0, second = 0; ///< Indices into Assembly::parts, first < second.
    geometry::Aabb overlap;            ///< Shared volume, assembly frame, mm.
};

/// All pairs of parts of `assembly` (in part order) whose bounding boxes share more than
/// `tolerance` mm in each of X, Y and Z, EXCEPT pairs whose shared volume is covered (within
/// `tolerance`) by the union of the grooves machined into either part - the deliberate
/// engagement of a grooved back in its grooves, of a frame panel in the stile / rail grooves,
/// or of a stub tenon (rail end with cheek cuts on faces A and B) in a stile groove. A groove
/// counts with its machined volume on any face: its centre line from (u1, v1) to (u2, v2), its
/// full width across and its depth into the face (only grooves parallel to a part axis count).
/// Parts are compared by their boxes, which is exact for the axis-aligned panels this module
/// generates; rotated parts are approximated by their (larger) bounding boxes. A part with a
/// RECTILINEAR outline (PartShape whose outer segments are all straight and axis-parallel,
/// e.g. the L-shaped corner panels) is compared by the rectangles of that outline (one per x
/// slab and y interval; cut-outs ignored); other shapes by their bounding box.
std::vector<Interpenetration> findInterpenetrations(const domain::Assembly& assembly,
                                                    double tolerance = core::kLengthEps);

/// Text naming both parts and the shared volume, e.g. "'Back panel' (cab/back) and 'Right side'
/// (cab/side.right) interpenetrate by 18 x 3 x 698 mm (x 582..600, y 547..550, z 11..709)".
std::string describe(const domain::Assembly& assembly, const Interpenetration& found);

} // namespace fos::furniture
