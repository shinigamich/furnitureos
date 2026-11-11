// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/part.hpp"

#include <cstddef>
#include <vector>

/// Grouping and sequencing of a part's operations for manual and machine production.
namespace fos::machining {

enum class ToolKind { Drill, GrooveCutter, PocketMill };
const char* toString(ToolKind k) noexcept;

/// Operations of one face made with one tool, in working order.
struct OperationGroup {
    domain::Face face = domain::Face::A;
    ToolKind tool = ToolKind::Drill;
    /// Drill: hole diameter. GrooveCutter: groove width. PocketMill: 0 (no tool is implied
    /// by a pocket contour). Sizes within core::kLengthEps of each other share a group.
    double toolSize = 0;
    /// Indices into Part::operations, in working order.
    std::vector<std::size_t> order;
    /// Rapid (non-cutting) travel in the face plane, mm: from the face origin (u,v)=(0,0)
    /// to the first operation's entry point and from each operation's exit point to the
    /// next entry point. No return move is counted.
    double rapidTravel = 0;
};

struct OperationOrder {
    std::vector<OperationGroup> groups; ///< Sorted by face (A,B,L1,L2,W1,W2), tool kind, tool size.
    double totalRapidTravel = 0;        ///< Sum of the groups' rapidTravel.
};

/// Groups by face, then tool, and orders each group with a NEAREST-NEIGHBOUR HEURISTIC:
/// start at the face origin, always continue with the closest not yet visited entry
/// point (ties: lower index in Part::operations). This is a heuristic, not an optimal
/// tour. Entry/exit points: hole = its centre; groove = (u1,v1) -> (u2,v2) as stored
/// (grooves are not reversed); pocket = first contour point for both (face origin for an
/// empty contour). Every group starts again at the face origin (tool change).
OperationOrder orderOperations(const domain::Part& part);

} // namespace fos::machining
