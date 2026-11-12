// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// The standard parameters of a cabinet and their formulas per construction variant.
/// README.md lists every name with its formula; `standardParameters()` is the executable form.
#include "fos/furniture/definition.hpp"

namespace fos::furniture {

/// Standard parameter names (all lengths in mm).
namespace param {
// --- inputs (constants by default; any may be replaced by an expression) ---
inline constexpr const char* kWidth = "width";                    ///< Overall width.
inline constexpr const char* kHeight = "height";                  ///< Overall height incl. plinth.
inline constexpr const char* kDepth = "depth";                    ///< Carcass depth (an Overlay back adds back_thickness).
inline constexpr const char* kThickness = "thickness";            ///< Carcass board (synchronised from the carcass material).
inline constexpr const char* kBackThickness = "back_thickness";   ///< Synchronised from the back material.
inline constexpr const char* kFrontThickness = "front_thickness"; ///< Synchronised from the front material.
inline constexpr const char* kPlinthHeight = "plinth_height";
inline constexpr const char* kPlinthSetback = "plinth_setback";
inline constexpr const char* kBackInset = "back_inset";
inline constexpr const char* kGrooveDepth = "groove_depth";
inline constexpr const char* kGrooveDistance = "groove_distance";
inline constexpr const char* kGrooveClearance = "groove_clearance";
inline constexpr const char* kGrooveFit = "groove_fit";
inline constexpr const char* kRailWidth = "rail_width";
inline constexpr const char* kShelfSetback = "shelf_setback";
inline constexpr const char* kShelfClearance = "shelf_clearance";
inline constexpr const char* kShelfPinFront = "shelf_pin_front";
inline constexpr const char* kShelfPinBack = "shelf_pin_back";
inline constexpr const char* kFrontReveal = "front_reveal";
inline constexpr const char* kFrontGap = "front_gap";
inline constexpr const char* kFrontOffset = "front_offset";
inline constexpr const char* kInsetGap = "inset_gap";
inline constexpr const char* kHingeOffset = "hinge_offset";
inline constexpr const char* kHingeShelfClearance = "hinge_shelf_clearance";
inline constexpr const char* kHandleEdgeOffset = "handle_edge_offset";
inline constexpr const char* kHandleEndOffset = "handle_end_offset";
inline constexpr const char* kJointEndOffset = "joint_end_offset";
inline constexpr const char* kJointMaxSpacing = "joint_max_spacing";
// --- derived (expressions chosen by the construction options) ---
inline constexpr const char* kCarcassBottom = "carcass_bottom";
inline constexpr const char* kCarcassHeight = "carcass_height";
inline constexpr const char* kInnerWidth = "inner_width";
inline constexpr const char* kSideBottom = "side_bottom";
inline constexpr const char* kSideTop = "side_top";
inline constexpr const char* kSideHeight = "side_height";
inline constexpr const char* kTopLength = "top_length";
inline constexpr const char* kTopX = "top_x";
inline constexpr const char* kBottomLength = "bottom_length";
inline constexpr const char* kBottomX = "bottom_x";
inline constexpr const char* kInteriorBottom = "interior_bottom";
inline constexpr const char* kInteriorTop = "interior_top";
inline constexpr const char* kInnerHeight = "inner_height";
inline constexpr const char* kInteriorDepth = "interior_depth";
inline constexpr const char* kShelfDepth = "shelf_depth";
inline constexpr const char* kBackEngagement = "back_engagement";
inline constexpr const char* kBackWidth = "back_width";
inline constexpr const char* kBackHeight = "back_height";
inline constexpr const char* kBackX = "back_x";
inline constexpr const char* kBackZ = "back_z";
inline constexpr const char* kBackRearY = "back_rear_y";
inline constexpr const char* kGrooveCenter = "groove_center";
inline constexpr const char* kGrooveWidth = "groove_width";
inline constexpr const char* kOverlayOuter = "overlay_outer";
inline constexpr const char* kOverlayShared = "overlay_shared";
} // namespace param

/// One standard parameter for a given set of construction options.
struct StandardParameter {
    std::string name;
    bool derived = false;       ///< true: `formula` is the standard expression; false: input with `defaultValue`.
    double defaultValue = 0.0;
    std::string formula;
    std::string description;
    std::optional<double> lowerBound;
};

/// All standard parameters (inputs first, then derived in dependency order) for `options`.
std::vector<StandardParameter> standardParameters(const ConstructionOptions& options);
/// Standard constraints (names start with "std.") for `options`.
std::vector<parametric::ConstraintDef> standardConstraints(const ConstructionOptions& options);

bool isStandardParameter(std::string_view name);
/// True for standard parameters whose formula is maintained by syncStandardParameters().
bool isDerivedParameter(std::string_view name);
/// True for thickness / back_thickness / front_thickness.
bool isMaterialSyncedParameter(std::string_view name);

/// Unsupported option combinations (Rails with OverSides top; SidesToFloor with OverSides
/// bottom) -> Unsupported error explaining what to change.
core::Result<void> validateConstruction(const ConstructionOptions& options);

/// Brings the graph in line with `definition.construction`: defines missing standard
/// parameters, rewrites the formula of every derived parameter that is not in
/// `pinnedParameters`, and replaces the "std." constraints. Atomic: on error (e.g. a
/// constraint is violated under the new options) the definition is unchanged.
core::Result<parametric::ChangeSet> syncStandardParameters(CabinetDefinition& definition);

/// A valid cabinet with default parameter values (600 x 720 x 560, 18 mm), an empty interior
/// and no materials assigned. Error only for unsupported option combinations.
core::Result<CabinetDefinition> makeCabinet(domain::AssemblyId id, std::string name,
                                            const ConstructionOptions& options = {});

} // namespace fos::furniture
