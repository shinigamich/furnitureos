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
/// `tolerance` mm in each of X, Y and Z, EXCEPT pairs whose shared volume lies entirely
/// (within `tolerance`) inside a groove machined into face A or B of one of the two parts
/// - the deliberate engagement of a grooved back in its grooves. A groove counts with its
/// machined volume: its centre line from (u1, v1) to (u2, v2), its full width across and its
/// depth into the face (only grooves parallel to a part axis are considered).
/// Parts are compared by their boxes, which is exact for the axis-aligned panels this module
/// generates; rotated parts are approximated by their (larger) bounding boxes.
std::vector<Interpenetration> findInterpenetrations(const domain::Assembly& assembly,
                                                    double tolerance = core::kLengthEps);

/// Text naming both parts and the shared volume, e.g. "'Back panel' (cab/back) and 'Right side'
/// (cab/side.right) interpenetrate by 18 x 3 x 698 mm (x 582..600, y 547..550, z 11..709)".
std::string describe(const domain::Assembly& assembly, const Interpenetration& found);

} // namespace fos::furniture
