// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Authoritative, serialisable description of a parametric cabinet. Everything here is plain
/// data (value types, copyable: a copy is a complete undo snapshot). The concrete parts are
/// DERIVED from it by generate() (generate.hpp). All lengths are millimetres.
#include "fos/core/result.hpp"
#include "fos/domain/model.hpp"
#include "fos/furniture/enums.hpp"
#include "fos/hardware/catalog.hpp"
#include "fos/parametric/parameter_graph.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fos::furniture {

/// The libraries a definition refers to by id.
struct Libraries {
    std::vector<domain::Material> materials;
    std::vector<domain::EdgeBand> edgeBands;
    std::vector<domain::HardwareDefinition> hardware;

    const domain::Material* findMaterial(const domain::MaterialId& id) const noexcept;
    const domain::EdgeBand* findEdgeBand(const domain::EdgeBandId& id) const noexcept;
    const domain::HardwareDefinition* findHardware(const domain::HardwareId& id) const noexcept;
};

/// A domain::Model holding the libraries and the given assemblies (for cutSize(), reports, ...).
domain::Model toModel(const Libraries& libraries, std::vector<domain::Assembly> assemblies);

/// Size of a child section along the split direction (or of one drawer front in a stack).
struct SectionSize {
    SizeKind kind = SizeKind::Weight;
    double value = 1.0;     ///< Fixed: mm (> 0). Weight: proportion (> 0). Unused for Expression.
    std::string expression; ///< Expression: text over the cabinet's parameter graph, mm (> 0).

    static SectionSize fixed(double mm) { return {SizeKind::Fixed, mm, {}}; }
    static SectionSize weight(double w = 1.0) { return {SizeKind::Weight, w, {}}; }
    static SectionSize expr(std::string text) { return {SizeKind::Expression, 0.0, std::move(text)}; }
    bool operator==(const SectionSize&) const = default;
};

/// A front closing the opening of a section (see README "Fronts").
struct FrontSpec {
    FrontKind kind = FrontKind::Door;
    OverlayKind overlay = OverlayKind::Full;
    HingeSide hingeSide = HingeSide::Left; ///< Single door only (a pair hinges on both sides).
    bool hinges = true;                    ///< Doors: place cup hinges (not available for Inset).
    HandlePosition handle = HandlePosition::Center;
    /// DrawerStack only: one entry per drawer front, BOTTOM to TOP; at least one.
    std::vector<SectionSize> drawerHeights;
    bool operator==(const FrontSpec&) const = default;
};

/// Node of the interior layout tree. PATHS: the root has the empty path ""; the k-th child
/// (1-based) of a section with path P has path "P.k" ("k" below the root), e.g. "2.1".
struct Section {
    SectionSize size;                 ///< Own size inside the parent's split (ignored for the root).
    SplitDirection split = SplitDirection::None;
    std::vector<Section> children;    ///< Used when split != None (at least 2).
    int shelfCount = 0;               ///< Leaf only: adjustable shelves, evenly distributed.
    std::optional<FrontSpec> front;   ///< Front covering this section's opening (leaf or split).
    bool operator==(const Section&) const = default;
};

/// Section at `path` ("" = root); NotFound error naming the path otherwise.
core::Result<Section*> findSection(Section& root, std::string_view path);
core::Result<const Section*> findSection(const Section& root, std::string_view path);

struct ConstructionOptions {
    JointKind topJoint = JointKind::BetweenSides;
    JointKind bottomJoint = JointKind::BetweenSides;
    TopKind top = TopKind::Panel;       ///< Rails requires topJoint == BetweenSides.
    BackMount back = BackMount::Overlay;
    PlinthKind plinth = PlinthKind::None; ///< SidesToFloor requires bottomJoint == BetweenSides.
    JointFastener topFastener = JointFastener::Confirmat;     ///< Side/top (or rail) corners.
    JointFastener bottomFastener = JointFastener::Confirmat;  ///< Side/bottom corners.
    JointFastener dividerFastener = JointFastener::Confirmat; ///< Partitions, fixed shelves, plinth panel.
    bool shelfSupports = true; ///< Drill shelf-pin holes under adjustable shelves.
    bool operator==(const ConstructionOptions&) const = default;
};

/// Per-part override keyed by slot id (e.g. "side.left", "shelf.2.1").
struct PartOverride {
    std::optional<domain::MaterialId> material;
    /// Present entry = override of that edge; std::nullopt value = raw edge.
    std::map<domain::EdgeSide, std::optional<domain::EdgeBandId>> edges;
    bool operator==(const PartOverride&) const = default;
};

struct MaterialAssignment {
    domain::MaterialId carcass; ///< Sides, top, bottom, rails, shelves, partitions, plinth.
    domain::MaterialId back;    ///< Required unless the back is BackMount::None.
    domain::MaterialId front;   ///< Doors and drawer fronts; empty = same as carcass.
    domain::EdgeBandId visibleEdge;                ///< Front edges of carcass parts; empty id = raw.
    std::optional<domain::EdgeBandId> hiddenEdge;  ///< Exposed end edges at through corners; unset = raw.
    std::optional<domain::EdgeBandId> frontEdge;   ///< All four edges of fronts; unset = visibleEdge.
    std::map<std::string, PartOverride> overrides; ///< Keyed by slot id.
    bool operator==(const MaterialAssignment&) const = default;
};

/// Catalogue ids of the fittings used by the generator (defaults: generic built-in catalogue).
struct HardwareSelection {
    domain::HardwareId hinge{hardware::ids::kHingeCup35};
    domain::HardwareId handle{hardware::ids::kHandleBar128};
    domain::HardwareId shelfPin{hardware::ids::kShelfPin5};
    domain::HardwareId confirmat{hardware::ids::kConfirmat7x50};
    domain::HardwareId dowel{hardware::ids::kDowel8x30};
    domain::HardwareId foot{hardware::ids::kFootAdjustable};
    bool operator==(const HardwareSelection&) const = default;
};

/// A parametric cabinet. `parameters` is the single source of truth for every dimension:
/// the generator reads sizes and positions only from graph values (see parameters.hpp for
/// the standard names and README.md for the formulas).
struct CabinetDefinition {
    domain::AssemblyId id;
    std::string name;
    geometry::Transform placement; ///< Assembly frame -> project frame.
    parametric::ParameterGraph parameters;
    /// Standard DERIVED or material-synchronised parameters the user has overridden; they are
    /// not rewritten when options or materials change (see syncStandardParameters()).
    std::set<std::string> pinnedParameters;
    MaterialAssignment materials;
    ConstructionOptions construction;
    HardwareSelection hardware;
    Section interior;
};

} // namespace fos::furniture
