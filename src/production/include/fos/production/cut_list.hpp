// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/production/part_list.hpp"

namespace fos::production {

struct CutListOptions {
    /// Merge identical parts that belong to different assemblies.
    bool groupAcrossAssemblies = true;
    /// Two sizes are "the same" when they differ by at most this many mm. A part is
    /// compared with the FIRST member of each existing group (so groups never drift).
    double sizeTolerance = core::kLengthEps;
};

/// Canonical text describing the machining of a part, used to keep parts with
/// different machining apart in the cut list (they are not interchangeable).
///
/// Definition: one token per operation, tokens sorted and joined with ';'.
///   Hole   -> "H|face|u|v|diameter|depth"   (depth is "T" for through holes)
///   Groove -> "G|face|u1|v1|u2|v2|width|depth"  with the two end points ordered
///             so that the direction of the centre line does not matter
///   Pocket -> "P|face|depth-or-T|u,v u,v ..."   contour points in stored order
/// Numbers are rounded to 0.001 mm. Operation ids, `source` and `note` are NOT part
/// of the signature; the order of operations is irrelevant. An empty string means
/// "no machining". Mirrored parts (e.g. left/right sides with holes on face B) have
/// different signatures by design. Two pockets with the same contour starting at a
/// different vertex are treated as different (no contour normalisation).
std::string operationSignature(const domain::Part& part);

/// A group of interchangeable parts.
struct CutListRow {
    domain::MaterialId materialId;
    std::string materialName;
    bool materialMissing = false;
    double thickness = 0;
    double cutLength = 0, cutWidth = 0; ///< Of the first member, mm.
    double length = 0, width = 0;       ///< Finished size of the first member, mm.
    domain::GrainDirection grain = domain::GrainDirection::None;
    /// True when the part may be turned 90 degrees on the sheet: grain is `None` or the
    /// material has no grain. For a missing material only `grain == None` allows rotation.
    bool rotationAllowed = false;
    std::array<EdgeCell, 4> edges;
    int quantity = 0;
    std::vector<domain::PartId> partIds; ///< In part-number order; size == quantity.
    std::vector<int> partNumbers;        ///< PartRow::number of each member.
    std::vector<std::string> partNames;  ///< Distinct member names in first-seen order.
    std::vector<domain::AssemblyId> assemblyIds; ///< Distinct, first-seen order.
    int operationCount = 0;              ///< Per part.
    std::string operationSignature;

    /// Distinct names joined with " / " (e.g. "Bottom / Top").
    std::string name() const;
};

struct CutList {
    std::vector<CutListRow> rows;
    core::Diagnostics diagnostics;
};

/// Cut list. Parts are merged only when ALL of these match: material id, thickness,
/// cut size and finished size (within `sizeTolerance`), edge band id on each of the
/// four sides, grain direction, operation signature and - unless
/// `groupAcrossAssemblies` - the owning assembly. Name and role do not matter.
/// Sorted by material name, material id, then descending cut length, descending cut
/// width, then lowest part number.
CutList cutList(const domain::Model& model, const CutListOptions& options = {}, const Scope& scope = {});

} // namespace fos::production
