// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Private: analysis of a straight run of cabinet items, shared by the kitchen builders
// (worktop.cpp, plinth.cpp). All lengths are mm; see worktop.hpp "RUN FRAME".
#include "fos/domain/model.hpp"
#include "fos/project/project.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace fos::project::detail {

inline constexpr double kRunEps = 1e-6;
inline constexpr double kRunFlushTolerance = 0.5; ///< Gap / overlap between neighbours treated as touching (mm).

/// Shortest fixed-notation decimal text that reads back as `v` (messages and expressions).
std::string runNum(double v);
/// Number rounded to 0.1 mm for messages.
std::string runMm(double v);

struct RunCabinet {
    std::string id;
    const Item* item = nullptr;      ///< Points into the analysed project.
    geometry::Transform relative;    ///< Cabinet assembly frame -> reference frame (translation only).
    domain::Assembly assembly;       ///< Generated cabinet (assembly frame).
    geometry::Aabb box;              ///< Bounds of the generated parts in the reference frame.
};

/// A gap wider than kRunFlushTolerance between two neighbouring cabinets.
struct RunGap {
    std::string leftId, rightId;
    double width = 0;
};

struct CabinetRun {
    geometry::Transform reference;   ///< Placement of the first listed cabinet.
    std::vector<RunCabinet> cabinets; ///< Sorted left to right (stable).
    std::vector<RunGap> gaps;        ///< Left to right.
    geometry::Aabb box;              ///< Union of the cabinet boxes.
};

/// Validates and measures the run of `itemIds` for a builder called `noun` ("worktop",
/// "plinth"; used in the messages). Errors (InvalidArgument / NotFound): no ids, an id listed
/// twice, a missing item, a free panel, a rotation different from the first item, a cabinet
/// that cannot be generated or has no parts, two cabinets overlapping along the run.
core::Result<CabinetRun> analyzeRun(const Project& project, const std::vector<std::string>& itemIds,
                                    std::string_view noun);

} // namespace fos::project::detail
