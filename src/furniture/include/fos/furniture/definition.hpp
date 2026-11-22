// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Authoritative, serialisable description of a parametric cabinet. Everything here is plain
/// data (value types, copyable: a copy is a complete undo snapshot). The concrete parts are
/// DERIVED from it by generate() (generate.hpp). All lengths are millimetres.
#include "fos/core/result.hpp"
#include "fos/domain/model.hpp"
#include "fos/furniture/contour_spec.hpp"
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

/// Drawer boxes behind the fronts of a drawer stack (README "Drawer boxes"). One box per
/// drawer front, carried by a pair of slides of the definition `slide` (kind DrawerSlide,
/// e.g. hardware::ids::kSlideBallBearing or kSlideUndermount) in the hardware library.
struct DrawerBoxSpec {
    domain::HardwareId slide{hardware::ids::kSlideBallBearing};
    DrawerBottomMount bottom = DrawerBottomMount::Grooved;
    /// Corner joints of the box (sides to inner front and back, README "Drawer boxes"):
    /// Dowel (HardwareSelection::dowel, default), Confirmat (HardwareSelection::confirmat,
    /// through the sides) or None (nothing drilled; the box is assembled by other means).
    JointFastener joint = JointFastener::Dowel;
    bool operator==(const DrawerBoxSpec&) const = default;
};

/// Sliding doors of a section (FrontKind::SlidingDoors, README "Sliding doors").
struct SlidingDoorSpec {
    /// Number of doors (kMinSlidingDoors..kMaxSlidingDoors) when `countExpression` is empty.
    int count = 2;
    /// Optional expression over the cabinet's parameter graph (e.g. "if(width > 2000, 3, 2)");
    /// it must evaluate to a whole number in kMinSlidingDoors..kMaxSlidingDoors.
    std::string countExpression;
    bool operator==(const SlidingDoorSpec&) const = default;
};
inline constexpr int kMinSlidingDoors = 2;
inline constexpr int kMaxSlidingDoors = 6;

/// A front closing the opening of a section (see README "Fronts"). For every generated front
/// (door or drawer front) face A is the inner, cabinet-side face and face B the visible face.
struct FrontSpec {
    FrontKind kind = FrontKind::Door;
    OverlayKind overlay = OverlayKind::Full;
    HingeSide hingeSide = HingeSide::Left; ///< Single door only (a pair hinges on both sides).
    /// Doors / flaps: place cup hinges (crank chosen by `overlay`); Flap: also the two lift
    /// mechanisms. false = front only.
    bool hinges = true;
    /// Sliding doors: None = no handle; any other value = one recessed grip (a Pocket on the
    /// visible face B) centred on the door width, at the given height.
    HandlePosition handle = HandlePosition::Center;
    /// SlidingDoors only: door count (ignored for other kinds).
    SlidingDoorSpec sliding;
    /// DrawerStack only: one entry per drawer front, BOTTOM to TOP; at least one.
    std::vector<SectionSize> drawerHeights;
    /// DrawerStack only: std::nullopt = fronts only; set = a drawer box with slides behind
    /// every front of the stack (ignored for doors).
    std::optional<DrawerBoxSpec> drawerBox;
    /// DrawerStack only: per-drawer boxes. Empty (default) = every drawer uses `drawerBox`.
    /// Otherwise exactly one entry per drawer front (aligned with `drawerHeights`, BOTTOM to
    /// TOP); each entry REPLACES `drawerBox` for its drawer (std::nullopt = no box behind that
    /// front). A size mismatch fails generation. Use drawerBoxFor() to read the effective box.
    std::vector<std::optional<DrawerBoxSpec>> drawerBoxes;
    bool operator==(const FrontSpec&) const = default;
};

/// Effective box of drawer `index` (0-based, bottom to top) of `front`: drawerBoxes[index]
/// when drawerBoxes is not empty (std::nullopt when index is out of range), else drawerBox.
std::optional<DrawerBoxSpec> drawerBoxFor(const FrontSpec& front, std::size_t index);
/// True when at least one drawer of `front` gets a box (drawerBoxFor of some index is set).
bool hasDrawerBoxes(const FrontSpec& front);

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
    /// Partitions, fixed shelves, plinth panel. SHARED-PANEL RULE: a joint whose face part is
    /// itself a divider (a fixed shelf against a partition, a partition on a fixed shelf) is
    /// always a blind dowel joint (HardwareSelection::dowel) with the face hole at most
    /// (thickness - 2) / 2 deep, because the divider may be drilled at the same spot from its
    /// other face; a confirmat's through hole would meet that joint (README "Shared panels").
    JointFastener dividerFastener = JointFastener::Confirmat;
    bool shelfSupports = true; ///< Drill shelf-pin holes under adjustable shelves.
    bool operator==(const ConstructionOptions&) const = default;
};

/// Per-part override keyed by slot id (e.g. "side.left", "shelf.2.1").
struct PartOverride {
    std::optional<domain::MaterialId> material;
    /// Present entry = override of that edge; std::nullopt value = raw edge.
    std::map<domain::EdgeSide, std::optional<domain::EdgeBandId>> edges;
    /// Parametric outline of the part (README "Contoured parts"); std::nullopt = rectangle.
    /// The edge bands above (and the generator's) are mapped onto the outline's segments.
    std::optional<ContourSpec> contour;
    bool operator==(const PartOverride&) const = default;
};

struct MaterialAssignment {
    domain::MaterialId carcass; ///< Sides, top, bottom, rails, shelves, partitions, plinth.
    domain::MaterialId back;    ///< Required unless the back is BackMount::None.
    domain::MaterialId front;   ///< Doors and drawer fronts; empty = same as carcass.
    domain::MaterialId drawer;  ///< Drawer-box sides, inner fronts, backs; empty = same as carcass.
    domain::MaterialId drawerBottom; ///< Drawer-box bottoms; empty = the back material (or `drawer` without one).
    domain::EdgeBandId visibleEdge;                ///< Front edges of carcass parts; empty id = raw.
    std::optional<domain::EdgeBandId> hiddenEdge;  ///< Exposed end edges at through corners; unset = raw.
    std::optional<domain::EdgeBandId> frontEdge;   ///< All four edges of fronts; unset = visibleEdge.
    std::map<std::string, PartOverride> overrides; ///< Keyed by slot id.
    bool operator==(const MaterialAssignment&) const = default;
};

/// Catalogue ids of the fittings used by the generator (defaults: generic built-in catalogue).
struct HardwareSelection {
    domain::HardwareId hinge{hardware::ids::kHingeCup35};           ///< Full overlay (0 mm crank).
    domain::HardwareId hingeHalf{hardware::ids::kHingeCup35Half};   ///< Half-overlay fronts.
    domain::HardwareId hingeInset{hardware::ids::kHingeCup35Inset}; ///< Inset fronts.
    domain::HardwareId lift{hardware::ids::kLiftGeneric};           ///< Flap lift mechanism (one per side).
    domain::HardwareId slidingSystem{hardware::ids::kSlidingGeneric}; ///< Track system of sliding doors.
    domain::HardwareId handle{hardware::ids::kHandleBar128};
    domain::HardwareId shelfPin{hardware::ids::kShelfPin5};
    domain::HardwareId confirmat{hardware::ids::kConfirmat7x50};
    domain::HardwareId dowel{hardware::ids::kDowel8x30};
    domain::HardwareId foot{hardware::ids::kFootAdjustable};
    bool operator==(const HardwareSelection&) const = default;
};

/// Custom parameter names read by the corner constructions (README "Corner cabinets"). They
/// are ordinary graph parameters (constants or expressions, mm); the corner templates define
/// them. A corner kind whose parameter is missing from the graph fails generation.
namespace corner_param {
/// Blind: front width of the blind part, from the OUTER face of the blind-side carcass side
/// to the centre of the gap between blind panel and door.
inline constexpr const char* kBlindWidth = "blind_width";
/// LShape: overall length of leg B along the left wall (assembly -Y direction, measured from
/// the rear edge of leg A), mm.
inline constexpr const char* kWidthB = "corner_width_b";
/// LShape: carcass depth of leg B (wall face to front edge, along X), mm.
inline constexpr const char* kDepthB = "corner_depth_b";
} // namespace corner_param

/// Door width limits of corner cabinets (blind corner door; each L-corner door), mm. A
/// GENERIC RULE OF THUMB for single cup-hinged doors, not a manufacturer specification.
inline constexpr double kMinCornerDoorWidth = 150.0;
inline constexpr double kMaxCornerDoorWidth = 600.0;
/// Narrowest blind panel of a blind corner cabinet (rule of thumb: a narrower strip cannot be
/// fixed reasonably), mm.
inline constexpr double kMinBlindPanelWidth = 50.0;

/// Corner construction (README "Corner cabinets").
///  * Blind: the root section's front must be a single Door hinged on the side AWAY from the
///    blind panel. The front is split at `blind_width`: a fixed blind panel (slot `blind`,
///    front material, no fittings generated: it is screwed on from inside) and the door.
///    Errors: blind panel narrower than kMinBlindPanelWidth, door outside
///    kMinCornerDoorWidth..kMaxCornerDoorWidth, wrong hinge side, missing parameter.
///  * LShape: `width` / `depth` are length and depth of leg A (along the rear wall, front at
///    y = 0), `corner_width_b` / `corner_depth_b` those of leg B (along the left wall at x = 0,
///    extending towards -Y). Requires BetweenSides joints, a Panel top, an Overlay or no back,
///    no plinth or SidesToFloor, a leaf root section (its shelfCount gives L-shaped shelves)
///    and a root front that is absent or a Full-overlay DoorPair (generated as two
///    independent cup-hinged doors; bi-fold hinges are not modelled).
struct CornerSpec {
    CornerKind kind = CornerKind::None;
    CornerSide blindSide = CornerSide::Left; ///< Blind only.
    bool operator==(const CornerSpec&) const = default;
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
    CornerSpec corner; ///< Default: ordinary rectangular cabinet.
};

} // namespace fos::furniture
