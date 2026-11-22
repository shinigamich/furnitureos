// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Internal state shared by the generator stages (carcass, back, interior, fronts, hardware).
#include "fos/furniture/generate.hpp"
#include "fos/furniture/parameters.hpp"
#include "fos/hardware/drawer_slides.hpp"
#include "fos/hardware/handle.hpp"
#include "fos/hardware/sliding_doors.hpp"
#include "fos/machining/face.hpp"

#include <array>
#include <optional>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fos::furniture::detail {

using geometry::Transform;
using geometry::Vec3;

inline constexpr Vec3 kRight{1, 0, 0}, kBack{0, 1, 0}, kUp{0, 0, 1}, kDown{0, 0, -1};

/// SHARED-PANEL RULE. An interior divider (partition or fixed shelf) can be drilled at the
/// same spot from both of its faces (one fitting per neighbouring compartment). Every blind
/// hole the generator drills into a face of such a panel therefore stops before the
/// mid-plane, leaving at least this much material (mm) between two opposite holes.
inline constexpr double kSharedPanelWeb = 2.0;
/// Deepest blind face hole allowed in a shared panel of thickness `t` (mm): (t - web) / 2,
/// e.g. 8 mm in an 18 mm and 7 mm in a 16 mm board.
inline constexpr double sharedHoleDepthLimit(double t) noexcept { return (t - kSharedPanelWeb) / 2.0; }
/// The generator places adjustable shelves exactly `shelf_clearance` from each support; when
/// it hands that geometry to the shelf-pin rule it allows this much extra gap (mm) so that
/// floating-point rounding of the placements can never make the rule miss a support.
inline constexpr double kShelfPinGapSlack = 0.5;

/// Which edge band an edge of a generated part receives.
enum class EdgeUse { Raw, Visible, Hidden, Front };
/// Indexed by domain::EdgeSide (L1, L2, W1, W2).
using Edges = std::array<EdgeUse, 4>;

enum class JobKind { Corner, ShelfPins, Hinges, Handle, Slide, Lift, SlidingTrack, SlidingRollers };
/// A deferred hardware placement; run after all parts exist (gen_hardware.cpp).
struct Job {
    JobKind kind = JobKind::Corner;
    std::string instance;   ///< Instance id suffix after "<cabinet>/".
    std::size_t a = 0;      ///< Corner: face part. ShelfPins: shelf. Hinges: door. Handle: front. Slide: box side.
    std::size_t b = 0;      ///< Corner: edge part. ShelfPins: left support. Hinges / Slide: carrying panel.
    std::size_t c = 0;      ///< ShelfPins: right support. Lift: panel above the flap.
    /// Hinges: hinge definition chosen from the front's overlay (empty = HardwareSelection::hinge).
    domain::HardwareId hinge;
    bool flap = false;      ///< Hinges: top-hinged flap (hinge axis X, b = panel above).
    JointFastener fastener = JointFastener::None; ///< Corner.
    /// Hinges: the carrying panel is a divider. Corner: the FACE part is a divider. ShelfPins:
    /// the left (b) support is a divider. Such panels follow the shared-panel rule above.
    bool sharedPanel = false;
    bool sharedC = false; ///< ShelfPins: the right (c) support is a divider.
    hardware::HandleConfig handle;
    /// SlidingTrack: a = panel carrying the track; instanceId is filled in when placed.
    /// sharedPanel limits the screw holes to the shared-panel depth. SlidingRollers: a = door.
    hardware::SlidingTrackConfig track;
    domain::HardwareId slide; ///< Slide: definition id (DrawerBoxSpec::slide).
    double slideLength = 0;   ///< Slide: nominal length, mm.
    /// Corner (drawer-box joints): fastener zone along the joint (CornerJointConfig::zone).
    std::optional<machining::Range> zone;
    /// Drawer-box joints and slides: a new hole that intersects a hole already drilled into
    /// the same part fails the placement (furniture.drawer_box.hole_collision).
    bool refuseCollisions = false;
};

/// The panels bounding a section (indices into Gen::assembly.parts).
struct Bounds {
    std::size_t left = 0, right = 0;
    std::vector<std::size_t> bottom, top; ///< Two entries for stretcher rails.
    bool leftOuter = true, rightOuter = true, bottomOuter = true, topOuter = true; ///< Carcass panel (not a divider).
};
/// Clear opening of a section in the assembly frame (x across, z up).
struct Box {
    double x0 = 0, x1 = 0, z0 = 0, z1 = 0;
    /// Front of the usable interior (y): 0, or behind the tracks of a sliding-door section
    /// (hardware::SlidingDoorPlan::interiorFrontY). Dividers and shelves start here.
    double y0 = 0;
};

struct Gen {
    const CabinetDefinition& def; ///< Resolved definition.
    const Libraries& libs;
    std::map<std::string, double> values;
    domain::Assembly assembly;
    core::Diagnostics diagnostics;
    std::vector<Job> jobs;
    /// Every shelf (adjustable or fixed) with each vertical panel it rests against:
    /// (shelf index, panel index). Recorded from the section tree, independent of whether
    /// shelf pins or joint fasteners are generated; used to keep hinges clear of shelves.
    std::vector<std::pair<std::size_t, std::size_t>> shelfCarriers;
    /// Slot -> index into assembly.parts, maintained by addPart() (O(1) index(); lookups
    /// only, never iterated, so the hash order cannot affect the output).
    std::unordered_map<std::string, std::size_t> slotIndex;

    /// Value of a parameter of the resolved graph (all standard names exist).
    double v(const char* name) const { return values.at(name); }
    /// Value of an OPTIONAL parameter (a custom parameter the user may add to the graph, e.g.
    /// the drawer-box clearances); `fallback` when the graph does not define it.
    double opt(const char* name, double fallback) const {
        const auto it = values.find(name);
        return it == values.end() ? fallback : it->second;
    }
    std::string partId(const std::string& slot) const { return def.id.str() + "/" + slot; }
    /// Index of the part with this slot; parts.size() when absent.
    std::size_t index(const std::string& slot) const;
    void warn(std::string code, std::string message, std::string entity);
    void error(std::string code, std::string message, std::string entity);

    /// Appends a part; material, grain and edge bands come from the role assignment and the
    /// per-slot overrides. `thickness` is the thickness parameter of the role.
    std::size_t addPart(const std::string& slot, std::string name, domain::PartRole role, MaterialRole material,
                        double length, double width, double thickness, const Transform& placement, const Edges& edges);
};

/// Material of a role (empty ids fall back as documented in MaterialAssignment).
domain::MaterialId roleMaterial(const MaterialAssignment& m, MaterialRole role);

/// "a.b" joining of slot segments; empty segments are skipped ("door", "", "left" -> "door.left").
std::string joinSlot(std::initializer_list<std::string_view> segments);
/// Display form of a section path: "root" for "".
std::string sectionLabel(const std::string& path);
/// Number for messages (formatLength with 3 decimals).
std::string num(double value);

/// Splits `available` mm among `sizes` (Fixed / Expression take their millimetres, Weight
/// children share the rest). `what` names the thing being split for error messages.
core::Result<std::vector<double>> distribute(const Gen& g, double available, const std::vector<SectionSize>& sizes,
                                             const std::string& what);

core::Result<Bounds> buildCarcass(Gen& g);
core::Result<void> buildBack(Gen& g);
core::Result<void> buildInterior(Gen& g, const Bounds& rootBounds);
core::Result<void> buildFront(Gen& g, const FrontSpec& spec, const std::string& path, const Box& box, const Bounds& bounds);
/// Sliding doors of the section at `path` (README "Sliding doors"): door parts, track and
/// roller jobs, recessed grips. Returns the y behind which the section's interior must stay.
core::Result<double> buildSlidingDoors(Gen& g, const FrontSpec& spec, const std::string& path, const Box& box,
                                       const Bounds& bounds);
/// Drawer box `ds` (README "Drawer boxes"; the effective drawerBoxFor() of this drawer) behind
/// the drawer front `frontSlot` of the stack `spec` whose front spans z in [fz0, fz1]; `box` /
/// `bounds` are the section's opening and bounding panels. Schedules the four corner-joint
/// jobs (unless ds.joint is None) and then the two slide jobs. Errors (ConstraintViolation /
/// NotFound) name the drawer.
core::Result<void> buildDrawerBox(Gen& g, const FrontSpec& spec, const DrawerBoxSpec& ds, const std::string& frontSlot,
                                  double fz0, double fz1, const Box& box, const Bounds& bounds);
/// Handle job for front part `front` (slot `slot`): door = vertical handle near the free edge
/// (`freeEdgeRight` = the free edge is the right / larger-coordinate one), else horizontal.
void handleJob(Gen& g, std::size_t front, const std::string& slot, const FrontSpec& spec, bool door, bool freeEdgeRight);
/// Structural checks of definition.corner (CornerSpec docs); InvalidArgument naming the cabinet.
core::Result<void> checkCorner(const Gen& g);
/// Blind corner (gen_corner_blind.cpp): adds the blind panel (slot `blind`) to the root front
/// spanning x in [x0, x1], z in [z0, z1] with its inner face at y = yInner and returns the
/// door's x range. Errors name blind_width, the panel / door widths and the limits.
core::Result<std::pair<double, double>> buildBlindPanel(Gen& g, double x0, double x1, double z0, double z1,
                                                        double yInner);
/// L-shaped corner cabinet (gen_corner_l.cpp): every part and the joint / hinge / handle jobs.
core::Result<void> buildLCorner(Gen& g);
/// L-shaped corner cabinet, after runHardware: shelf-pin holes in both end sides (dropped
/// with furniture.shelf_pins.dropped when they would meet a joint hole).
void placeLCornerShelfPins(Gen& g);
/// L-shaped corner cabinet, after applyContours: machining of the L-shaped parts must lie
/// inside their outlines (furniture.contour.machining_outside).
void checkLCornerOutlines(Gen& g);
void runHardware(Gen& g);
/// Refuses an assembly with more than limits::kMaxPartsPerAssembly parts or a part whose
/// length / width / thickness is not finite, not > 0 or above limits::kMaxDimension
/// (message starts with "furniture.geometry.invalid_size").
core::Result<void> checkPartSizes(const Gen& g);
/// Turns the contours of the per-slot overrides into part shapes (after hardware placement,
/// so that machining outside an outline is reported as furniture.contour.machining_outside).
/// Invalid contours fail generation (messages start with "furniture.contour.invalid:").
core::Result<void> applyContours(Gen& g);
/// Self-check after generation: furniture.geometry.interpenetration errors (checks.hpp).
void checkGeometry(Gen& g);

} // namespace fos::furniture::detail
