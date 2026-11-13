// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Hard size limits of a cabinet definition (README "Limits"). They exist so that a hostile or
/// corrupted definition (e.g. from a project file) cannot make generation run for minutes or
/// produce infinite / absurd geometry. They are far above any real cabinet. generate() (and
/// with it every edit function of edit.hpp) refuses a definition that exceeds them; the
/// project codec rejects such files on load with the same messages.
#include "fos/core/result.hpp"
#include "fos/furniture/definition.hpp"

#include <cstddef>

namespace fos::furniture {

namespace limits {
/// Largest absolute value (mm) of any standard (length) parameter, of a Fixed section or
/// drawer size, and of the length / width / thickness of any generated part. 100 m.
inline constexpr double kMaxDimension = 100000.0;
/// Largest Weight (proportion) of a section or drawer size.
inline constexpr double kMaxWeight = 1000000.0;
/// Adjustable shelves of one leaf section.
inline constexpr int kMaxShelvesPerSection = 100;
/// Adjustable shelves of the whole cabinet (all sections together). Shelf pins of all
/// shelves resting against one panel are drilled into that panel, and hardware placement
/// and machining validation cost grows with the square of the holes per panel.
inline constexpr std::size_t kMaxShelvesPerCabinet = 200;
/// Child sections of one split section.
inline constexpr std::size_t kMaxSectionChildren = 50;
/// Sections of the whole interior tree, the root included.
inline constexpr std::size_t kMaxSections = 250;
/// Nesting depth of the interior tree; the root has depth 0, its children depth 1.
inline constexpr std::size_t kMaxSectionDepth = 16;
/// Drawer fronts of one drawer stack.
inline constexpr std::size_t kMaxDrawerHeights = 30;
/// Parts of one generated assembly (checked on the estimate before generation and on the
/// generated assembly).
inline constexpr std::size_t kMaxPartsPerAssembly = 2000;
/// Carcass parts at most (sides, bottom, top or two rails, plinth, back), used by the estimate.
inline constexpr std::size_t kMaxCarcassParts = 8;
} // namespace limits

/// Counts of an interior tree, as used by the limit checks.
struct SectionTreeStats {
    std::size_t sections = 0;   ///< Nodes, the root included.
    std::size_t maxDepth = 0;   ///< Deepest node (root = 0).
    std::size_t shelves = 0;    ///< Adjustable shelves (negative shelf counts count as 0).
    std::size_t dividers = 0;   ///< Partitions + fixed shelves: (children - 1) per split node.
    std::size_t fronts = 0;     ///< Door 1, door pair 2, drawer stack = number of drawer heights.
};

/// Walks the tree (iteratively; it never recurses, so any depth is safe).
SectionTreeStats sectionTreeStats(const Section& root);

/// Upper bound of the number of parts generate() makes: kMaxCarcassParts + dividers +
/// shelves + fronts.
std::size_t estimatePartCount(const Section& root);

/// Checks the STRUCTURAL limits of the interior tree: shelves per section and per cabinet,
/// children per split, number of sections, depth, drawers per stack, estimated parts; and the
/// Fixed sizes of sections and drawers (finite, at most kMaxDimension). ConstraintViolation
/// (InvalidArgument for a non-finite or too large Fixed size) naming the section path, the
/// value and the limit. `owner` names the cabinet in messages.
core::Result<void> checkSectionLimits(const Section& root, const std::string& owner);

/// checkSectionLimits plus every STANDARD parameter (all are lengths) whose value can be
/// evaluated must be finite with |value| <= kMaxDimension (parameters that cannot be
/// evaluated are left to generate(), which reports them). Custom parameters are not limited
/// here; sizes derived from them are checked on the generated parts.
core::Result<void> checkDefinitionLimits(const CabinetDefinition& definition);

} // namespace fos::furniture
