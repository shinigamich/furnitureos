// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Kitchen WORKTOP (countertop) builder: one continuous top over a straight run of cabinet items,
/// stored as one or more free panel items (PanelItem, role Worktop) with a parametric contour
/// holding the sink / hob cut-outs (ADR-0008), so cutting/nesting, CAM, DXF export and drawings
/// handle them like any other shaped panel. All lengths are mm.
///
/// RUN FRAME. The first item of WorktopRequest::itemIds is the REFERENCE: every cabinet must have
/// the same rotation as it (a straight run), and the run is measured in its assembly frame
/// (X = along the run, left -> right seen from the front; Y = depth, front at the smallest y;
/// Z = up). The extent of each cabinet is the bounding box of its GENERATED parts (fronts and
/// plinths included), so the front overhang is measured from the foremost generated part, e.g.
/// the door faces. The run start (x = 0 of cut-out positions) is the left face of the leftmost
/// cabinet.
///
/// GEOMETRY of the finished worktop (edge banding included), in the reference frame:
///   x from runMinX - leftOverhang to runMaxX + rightOverhang,
///   y from runMinY - frontOverhang (front edge) to runMaxY - backGap (back edge; the wall is
///     assumed to touch the backs of the cabinets, so backGap is the gap to the wall),
///   z from the highest cabinet top upwards by the material thickness.
/// Each piece is a Horizontal panel: part x along the run, part y = depth with y = 0 at the
/// FRONT, so edge L1 is the front edge, L2 the back, W1 the left end and W2 the right end.
///
/// JOINTS. When the length exceeds the usable stock length (material sheetLength minus the
/// project cutting trims of that material, or WorktopRequest::maxPieceLength), the top is split
/// into the fewest equal pieces that fit, joined by plain BUTT JOINTS (square ends, raw, no
/// mitre, no connectors are generated: worktop connector bolts and their pockets are NOT
/// modelled). A joint that would come too close to a cut-out is moved to the nearest position
/// that keeps every piece within the stock length; when none exists the request fails.
///
/// CUT-OUT RULES are RULES OF THUMB (common shop practice, not a standard and not a
/// manufacturer's specification; always check the sink / hob installation sheet): see
/// WorktopRules for the default distances. Violations refuse the request with a message naming
/// the cut-out, the distance and the rule.
#include "fos/project/commands.hpp"
#include "fos/project/project.hpp"

#include <optional>
#include <string>
#include <vector>

namespace fos::project {

/// Generic placeholder worktop material added to the library when a request names no material
/// (38 mm, stock 4100 x 650, grain/decor along the length, cost 0 = unknown; not a product).
inline constexpr const char* kGenericWorktopMaterial = "mat.generic.worktop38";
/// Generic placeholder edge band (ABS 2 x 45, cost 0) added when the front must be banded, the
/// request names no band and no library band is wide enough for the worktop thickness.
inline constexpr const char* kGenericWorktopEdgeBand = "eb.generic.abs2x45";

enum class WorktopCutoutKind { Sink, Hob };
const char* toString(WorktopCutoutKind kind) noexcept;

/// A rectangular (optionally rounded-rectangle) through cut-out.
struct WorktopCutout {
    WorktopCutoutKind kind = WorktopCutoutKind::Sink;
    double centerX = 0;      ///< Centre along the run, measured from the run start (mm).
    double length = 0;       ///< Size along the run (mm, > 0).
    double depth = 0;        ///< Size front-to-back (mm, > 0).
    double cornerRadius = 0; ///< mm, 0 = sharp; at most half the smaller size.
    /// Distance from the finished FRONT edge of the worktop to the cut-out (mm). nullopt =
    /// centred in the worktop depth.
    std::optional<double> frontSetback;
};

/// Minimum distances checked for every cut-out (RULES OF THUMB, see the file comment).
struct WorktopRules {
    double minEdgeDistance = 50.0;   ///< Cut-out to the front edge and to the back edge.
    double minEndDistance = 100.0;   ///< Cut-out to a free end of the worktop.
    double minJointDistance = 150.0; ///< Cut-out to a butt joint.
    double minCutoutGap = 100.0;     ///< Between two cut-outs.
};

struct WorktopRequest {
    std::string id = "worktop";   ///< Item id; pieces of a split top are "<id>.1", "<id>.2", ...
    std::string name = "Worktop"; ///< Item name; pieces "<name> 1/2", ...
    std::vector<std::string> itemIds; ///< Cabinet items of the run (any order; first = reference).
    std::string materialId;       ///< Empty = kGenericWorktopMaterial (added when missing).
    /// Front edge band. Empty = the first library band (by id) whose width covers the
    /// thickness, else kGenericWorktopEdgeBand (added). Ignored when bandFront is false.
    std::string frontBandId;
    bool bandFront = true;
    bool bandBack = false;     ///< Back edge (L2) with the front band; default raw.
    bool bandLeftEnd = false;  ///< Free left end (W1) with the front band; joints are always raw.
    bool bandRightEnd = false; ///< Free right end (W2) with the front band.
    double frontOverhang = 20.0; ///< Beyond the foremost generated part, [0, 300].
    double leftOverhang = 0.0;   ///< [0, 1000].
    double rightOverhang = 0.0;  ///< [0, 1000].
    double backGap = 0.0;        ///< Gap to the wall behind the cabinets, [0, 100].
    /// Longest piece; nullopt = usable stock length (see the file comment). Must be > 0 and
    /// not longer than the usable stock length.
    std::optional<double> maxPieceLength;
    std::vector<WorktopCutout> cutouts;
    WorktopRules rules;
};

struct WorktopPlan {
    std::vector<Item> items;  ///< One PanelItem per piece, left to right; placement = reference item's.
    std::optional<domain::Material> addedMaterial;  ///< Generic material to add to the library.
    std::optional<domain::EdgeBand> addedEdgeBand;  ///< Generic band to add to the library.
    double length = 0;    ///< Finished total length along the run.
    double depth = 0;     ///< Finished depth.
    double thickness = 0; ///< Material thickness.
    double runStart = 0;  ///< Reference-frame x of the run start (left face of the leftmost cabinet).
    double left = 0;      ///< Reference-frame x of the worktop's left end.
    double front = 0;     ///< Reference-frame y of the finished front edge.
    double top = 0;       ///< Reference-frame z of the worktop's underside (= highest cabinet top).
    double usableStockLength = 0;      ///< Longest piece allowed.
    std::vector<double> joints;        ///< Butt joint positions from the worktop's left end, ascending.
    std::vector<std::string> runOrder; ///< Cabinet ids sorted left to right.
    /// Findings that do not refuse the request: project.worktop.gap (Warning, a gap between two
    /// cabinets is bridged), project.worktop.front_offset (Info, cabinet fronts not flush),
    /// project.worktop.butt_joint (Info, one per joint), project.worktop.library_added (Info).
    core::Diagnostics notes;
};

/// Plans the worktop without changing the project. Errors (InvalidArgument / NotFound /
/// AlreadyExists, naming the item, cut-out and numbers): no or duplicate item ids, an item that
/// is missing, not a cabinet or cannot be generated, a rotation different from the reference,
/// overlapping cabinets, tops at different heights (more than 1 mm), options out of range, an
/// unknown material / band, a band narrower than the thickness, a depth wider than the usable
/// stock width, a cut-out violating WorktopRules or not fitting, no valid joint layout, an item
/// id of a piece already in use.
core::Result<WorktopPlan> planWorktop(const Project& project, const WorktopRequest& request);

/// planWorktop, then adds the library entries and appends the piece items. On error the project
/// is unchanged.
core::Result<WorktopPlan> addWorktop(Project& project, const WorktopRequest& request);

namespace commands {
/// Undoable addWorktop (label "Add worktop {0} over {1}"): Affected = the new items (+ libraries
/// when a generic material / band was added).
Command addWorktop(WorktopRequest request);
} // namespace commands

} // namespace fos::project
