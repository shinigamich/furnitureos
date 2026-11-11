// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/ids.hpp"
#include "fos/geometry/vec.hpp"

#include <string>
#include <variant>
#include <vector>

/// Machine-independent machining operations attached to a part.
///
/// PART FRAME. A part occupies the box [0,L] x [0,W] x [0,T] in its local right-handed
/// frame: X = length, Y = width, Z = thickness.
///
/// FACES and their 2D face coordinates (u, v):
///   A  : z = T plane.  u = local x, v = local y.  Tool enters along -Z.
///   B  : z = 0 plane.  u = local x, v = local y.  Tool enters along +Z.
///        (A and B deliberately share the same (u,v) = (x,y); B is NOT mirrored.)
///   L1 : y = 0 edge.   u = local x, v = local z.  Tool enters along +Y.
///   L2 : y = W edge.   u = local x, v = local z.  Tool enters along -Y.
///   W1 : x = 0 edge.   u = local y, v = local z.  Tool enters along +X.
///   W2 : x = L edge.   u = local y, v = local z.  Tool enters along -X.
/// Depth is always measured from the face surface into the material, in mm.
/// Edge-face coordinates refer to the FINISHED part (including edge banding).
namespace fos::domain {

enum class Face { A, B, L1, L2, W1, W2 };

struct Hole {
    Face face = Face::A;
    double u = 0, v = 0;   ///< Centre in face coordinates.
    double diameter = 5.0;
    double depth = 0.0;    ///< Ignored when `through` is true.
    bool through = false;
    bool operator==(const Hole&) const = default;
};

/// Straight groove/slot of constant width and depth; (u1,v1)-(u2,v2) is the centre line.
struct Groove {
    Face face = Face::B;
    double u1 = 0, v1 = 0, u2 = 0, v2 = 0;
    double width = 4.0;
    double depth = 8.0;
    bool operator==(const Groove&) const = default;
};

/// Closed pocket milled to constant depth. Contour is counter-clockwise in face coordinates.
struct Pocket {
    Face face = Face::A;
    std::vector<geometry::Vec2> contour;
    double depth = 0.0;
    bool through = false;        ///< Cut-out through the whole thickness.
    bool operator==(const Pocket&) const = default;
};

using OperationShape = std::variant<Hole, Groove, Pocket>;

struct Operation {
    OperationId id;
    OperationShape shape;
    /// Origin of the operation: "user", or the id of the hardware instance / construction
    /// rule that generated it. Generated operations are replaced on recalculation.
    std::string source = "user";
    std::string note;
};

const char* toString(Face f) noexcept;
Face faceOf(const Operation& op) noexcept;

} // namespace fos::domain
