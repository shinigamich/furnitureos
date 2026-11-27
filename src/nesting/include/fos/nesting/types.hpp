// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/geometry/polygon.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// Plain input/output data of the true-shape nesting HEURISTIC (CNC routing of non-rectangular
/// parts on rectangular sheets).
///
/// SHEET FRAME: origin at the lower-left corner of the stock sheet, +X along the sheet LENGTH,
/// +Y along the sheet WIDTH. All lengths in mm, areas in mm^2, angles in degrees (CCW positive).
///
/// PART FRAME: the frame in which Part::outline is given. A placement maps a part-frame point p
/// to the sheet point  R(rotationDeg) * M * p + translation,  where M is the identity, or the
/// mirror x -> -x when `mirrored` (applied FIRST), and R a CCW rotation about the part-frame
/// origin. In geometry terms: translated(rotated(mirrored ? mirroredX(outline, 0) : outline,
/// rotationDeg), translation).
namespace fos::nesting {

/// Sheet::quantity value meaning "as many sheets as needed".
inline constexpr int kUnlimited = -1;

/// Material excluded at each sheet side (clamping, damaged edges). left: x = 0 side,
/// right: x = length side, bottom: y = 0 side, top: y = width side.
struct Margins {
    double left = 0, right = 0, bottom = 0, top = 0;
};

/// A type of rectangular stock sheet.
struct Sheet {
    std::string id;    ///< Unique within the job.
    std::string label;
    double length = 0; ///< Extent along X.
    double width = 0;  ///< Extent along Y.
    int quantity = kUnlimited; ///< Sheets available (>= 0) or kUnlimited.
    Margins margins;
};

/// Rotations a part may be placed with.
enum class RotationRule {
    None,    ///< 0 degrees only.
    Half,    ///< 0 and 180 degrees (keeps a grain direction).
    Quarter, ///< 0, 90, 180, 270 degrees.
    Step,    ///< k * step for k = 0, 1, ... while k * step < 360 (step from Part or NestOptions).
};

/// A required part: a simple polygon outline (either winding; do not repeat the first vertex).
struct Part {
    std::string id;    ///< Unique within the job.
    std::string label; ///< Used in messages and drawings.
    geometry::Polygon2 outline;
    /// Inner cut-outs (simple, pairwise disjoint polygons inside the outline). They reduce the
    /// reported net area; with NestOptions::partInPart other parts may be nested inside them.
    std::vector<geometry::Polygon2> holes;
    int quantity = 1; ///< >= 1.
    RotationRule rotation = RotationRule::Quarter;
    /// Step in degrees for RotationRule::Step, in (0, 360]; 0 = use NestOptions::rotationStepDeg.
    double rotationStepDeg = 0;
    /// Whether the mirror image may be placed as well (false for one-sided / decor boards).
    bool mirrorAllowed = false;
};

/// One nesting job (one material and thickness).
struct Job {
    std::vector<Sheet> sheets; ///< Opened in this order (first type with stock left that fits).
    std::vector<Part> parts;
    /// Minimum Euclidean distance between the outlines of any two parts, mm. Set it to
    /// tool diameter + extra clearance: a single router pass between two parts removes a
    /// channel exactly one tool diameter wide, so two outlines must be at least that far apart
    /// (plus any clearance wanted for tool deflection / tabs). 0 = parts may touch.
    double partSpacing = 0;
    /// Minimum distance between a part outline and the sheet margin line (the sheet inset by
    /// its margins), mm. Usually the tool diameter so the cut stays out of the margin. Parts
    /// must lie inside the PLACEMENT AREA = sheet inset by (margin + edgeSpacing) on each side.
    double edgeSpacing = 0;
    /// The free strip right of the used length is reported as a reusable remnant only when
    /// its length (along X) is at least this, mm.
    double minRemnantLength = 0;
};

/// Placement criterion of the bottom-left-fill decoder: which candidate position (among the
/// no-fit-polygon candidates) a part takes. All keys grow with x and y. Ties keep the earlier
/// orientation.
enum class PlacementScore {
    MinRightEdge,   ///< Smallest right bbox edge (x + w), then smallest bottom y (packs along X).
    BottomLeft,     ///< Smallest top bbox edge (y + h), then smallest left x (packs along Y).
    MinBoundingBox, ///< Smallest area of the sheet's used bounding box after placing, then x + w, y.
};

/// Deterministic starting orders of the part instances (all descending, ties by part order).
enum class InstanceOrder {
    AreaDesc,        ///< Outline area.
    LongestSideDesc, ///< Longest side of the unrotated bounding box.
    BoxAreaDesc,     ///< Unrotated bounding-box area.
    PerimeterDesc,   ///< Outline perimeter.
};

/// Controls of nest(). Fully deterministic for fixed inputs, seed and budgets, except when a
/// time budget is set (timeBudgetMs > 0), which depends on the machine speed.
struct NestOptions {
    /// Improvement iterations (seeded reorders) from the best starting layout.
    /// The budget is a COUNT, not a time, so results are reproducible on every machine.
    int iterations = 20;
    /// Rotation candidate set in degrees, each in [0, 360). Empty = no restriction. When given, a
    /// part's allowed rotations are its rule's angles intersected with this set (so grain-locked
    /// RotationRule::Half parts keep at most 0/180); a part left with no angle is a job error.
    std::vector<double> rotationSetDeg;
    /// Placement criteria tried as restarts (each with every starting order), non-empty.
    std::vector<PlacementScore> placementScores{PlacementScore::MinRightEdge, PlacementScore::BottomLeft,
                                                PlacementScore::MinBoundingBox};
    /// Deterministic starting orders tried for each placement score, non-empty.
    std::vector<InstanceOrder> orders{InstanceOrder::AreaDesc, InstanceOrder::LongestSideDesc,
                                      InstanceOrder::BoxAreaDesc, InstanceOrder::PerimeterDesc};
    /// Additional starting orders per placement score: seeded shuffles (seed-derived LCG), >= 0.
    int randomRestarts = 0;
    /// Allow parts inside the cut-outs (Part::holes) of other placed parts, fully inside the
    /// hole and at least Job::partSpacing from its boundary. Default off.
    bool partInPart = false;
    /// Total number of complete layouts (decodes) evaluated, 0 = unlimited (>= 0). The first
    /// layout is always evaluated. Deterministic.
    int maxDecodes = 0;
    /// Wall-clock budget in milliseconds, 0 = none (>= 0). NOT deterministic across machines.
    double timeBudgetMs = 0;
    /// Seed of the internal 32-bit linear congruential generator.
    std::uint32_t seed = 12345u;
    /// Default step for RotationRule::Step parts without their own step, degrees in (0, 360].
    double rotationStepDeg = 15.0;
    /// Polled between part placements and iterations; return true to stop. May be empty.
    std::function<bool()> isCancelled;
};

/// One placed part instance.
struct Placement {
    std::string partId;
    int instance = 0;          ///< 0 .. quantity-1.
    double rotationDeg = 0;    ///< In [0, 360).
    bool mirrored = false;
    geometry::Vec2 translation; ///< Image of the part-frame origin in the sheet frame.
    geometry::Polygon2 outline; ///< Transformed outline in the sheet frame (same vertex order rule as geometry).
};

/// One used stock sheet.
struct SheetLayout {
    std::string sheetId;  ///< Sheet::id of the stock type.
    int copy = 0;         ///< 0-based index among the sheets of that type used.
    std::vector<Placement> placements;
    double usedLength = 0;    ///< Max X over all placed outlines (sheet frame), mm.
    /// Free strip [usedLength + partSpacing, sheet length] x [0, sheet width] (raw sheet
    /// material incl. its margins), valid when hasRemnant.
    geometry::Rect2 remnant;
    bool hasRemnant = false;
    double partArea = 0;      ///< Sum of outline areas (holes NOT subtracted), mm^2.
    double netPartArea = 0;   ///< Sum of outline areas minus hole areas, mm^2.
    double sheetArea = 0;     ///< length * width, mm^2.
    double utilisation = 0;   ///< partArea / sheetArea (can exceed 1 with part-in-part).
    double netUtilisation = 0; ///< netPartArea / sheetArea (material actually used, <= 1).
};

enum class UnplacedReason {
    TooLarge,       ///< Does not fit an empty sheet of any type in any allowed orientation.
    StockExhausted, ///< Would fit an empty sheet, but no sheet with free room was left.
};

struct Unplaced {
    std::string partId;
    int instance = 0;
    UnplacedReason reason = UnplacedReason::StockExhausted;
    std::string message; ///< Specific English sentence with the numbers involved.
};

struct NestResult {
    std::vector<SheetLayout> sheets;
    std::vector<Unplaced> unplaced;
    int placedCount = 0;
    double totalPartArea = 0;     ///< Outline areas of placed instances, mm^2.
    double totalNetPartArea = 0;  ///< Same minus hole areas, mm^2.
    double totalSheetArea = 0;    ///< Areas of all used sheets, mm^2.
    double utilisation = 0;       ///< totalPartArea / totalSheetArea (0 when no sheet used).
    double netUtilisation = 0;    ///< totalNetPartArea / totalSheetArea (0 when no sheet used).
    /// Area lower bound on the sheets needed for ALL instances: ceil(sum of instance areas /
    /// largest placement area of any sheet type), instance area = net area with partInPart,
    /// outline area otherwise (parts lie disjoint inside the placement area). Not an optimum.
    int sheetLowerBound = 0;
    int iterationsRun = 0;        ///< Improvement iterations actually evaluated.
    int decodesRun = 0;           ///< Complete layouts evaluated (starts + improvements).
    PlacementScore placementScoreUsed = PlacementScore::MinRightEdge; ///< Criterion of the kept layout.
    bool cancelled = false;       ///< True when isCancelled stopped the run early.
    bool isHeuristic = true;      ///< Always true: the layout is NOT proven optimal.
};

} // namespace fos::nesting
