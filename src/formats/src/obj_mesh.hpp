// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Private: the OBJ export triangulates the cap faces of shaped parts with
// geometry::triangulateWithHoles (moved to the geometry module so the viewport and other
// consumers share one implementation). The old names are kept for the formats code and tests.
#include "fos/geometry/triangulate.hpp"

namespace fos::formats::detail {
inline constexpr std::size_t kMaxCapPoints = geometry::kMaxTriangulationPoints;
using CapTriangulation = geometry::TriangulationWithHoles;
using geometry::triangulateWithHoles;
} // namespace fos::formats::detail
