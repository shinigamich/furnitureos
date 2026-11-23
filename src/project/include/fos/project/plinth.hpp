// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Kitchen PLINTH (toe-kick) builder: a continuous front plinth board under a straight run of
/// cabinets standing on feet, stored as free panel items (PanelItem, role Plinth), so cutting,
/// CAM, DXF export and drawings handle them like any other panel. All lengths are mm.
///
/// RUN FRAME: the same as the worktop builder (worktop.hpp "RUN FRAME"; the run analysis is
/// shared): the first item of PlinthRequest::itemIds is the reference, every cabinet must have
/// its rotation, X = along the run, Y = depth (front at the smallest y), Z = up. The floor is
/// z = 0 of the cabinets' assembly frames (every cabinet must stand on the same floor level).
///
/// WHICH CABINETS. Ordinary cabinets must use PlinthKind::Feet: their carcass is raised by
/// `plinth_height` and the plinth closes the space in front of the feet. A cabinet with
/// SidesToFloor already generates its own recessed plinth panel and its sides reach the floor
/// (refused); a cabinet without plinth space (PlinthKind::None) is refused. APPLIANCE GAPS
/// (CabinetDefinition::applianceGap, e.g. a dishwasher) are accepted with any plinth kind:
/// their end panels reach the floor, so the plinth is INTERRUPTED at every end panel and the
/// strip in front of the appliance becomes its own piece (project.plinth.appliance note).
///
/// GEOMETRY. Each piece is a VerticalFront panel: part x along the run, part y = height (y = 0
/// at the floor), thickness towards the back. Front face at y = carcassFront + setback, where
/// carcassFront is the foremost carcass front of the run (y = 0 of the cabinet assembly frames;
/// fronts/doors stand in front of it). Height: PlinthRequest::height, else the common
/// `plinth_height` of the Feet cabinets (they must agree within 1 mm). Along the run the plinth
/// spans the run's generated parts (left face of the leftmost to right face of the rightmost
/// cabinet), minus the appliance end panels; gaps between cabinets are bridged (warning).
/// Each segment longer than the usable stock length (material sheetLength minus the cutting
/// trims, or maxPieceLength) is split into the fewest EQUAL pieces with plain BUTT JOINTS
/// (no connectors, no plinth clips: clips/feet fixing hardware is NOT modelled).
///
/// END RETURNS (optional): a VerticalSide panel at a free end of the run, its outer face flush
/// with the outer face of the end cabinet, from the back face of the front plinth to the back
/// of that cabinet (its generated parts), butting behind the front plinth.
///
/// RULES OF THUMB (common shop practice, not a standard; check the cabinet / appliance sheets):
/// the default setback of 50 mm behind the carcass front, and the note that integrated
/// dishwashers often need a lower or notched plinth piece for the door swing.
#include "fos/project/commands.hpp"
#include "fos/project/project.hpp"

#include <optional>
#include <string>
#include <vector>

namespace fos::project {

/// Default setback of the plinth face behind the carcass front (mm, RULE OF THUMB).
inline constexpr double kDefaultPlinthSetback = 50.0;

struct PlinthRequest {
    std::string id = "plinth";    ///< Item id; pieces "<id>.1", "<id>.2", ...; returns "<id>.left" / "<id>.right".
    std::string name = "Plinth";  ///< Item name; pieces "<name> 1/2", ...; returns "<name> return left" / "... right".
    std::vector<std::string> itemIds; ///< Cabinet items of the run (any order; first = reference).
    /// Empty = the carcass material of the reference (first listed) cabinet.
    std::string materialId;
    double setback = kDefaultPlinthSetback; ///< Plinth face behind the foremost carcass front, [0, 300].
    /// Plinth board height (floor upwards); nullopt = the Feet cabinets' plinth_height. Must be
    /// > 0 and not above that plinth_height (+1 mm); at most 300 when no Feet cabinet is in the run.
    std::optional<double> height;
    bool returnLeft = false;  ///< End return at the left end of the run.
    bool returnRight = false; ///< End return at the right end of the run.
    /// Longest piece; nullopt = usable stock length. Must be > 0 and at most that length.
    std::optional<double> maxPieceLength;
};

struct PlinthPlan {
    /// Front pieces left to right, then the left return, then the right return; placement = the
    /// reference item's.
    std::vector<Item> items;
    std::size_t frontPieces = 0; ///< Number of front pieces (the first items).
    double height = 0;     ///< Plinth board height.
    double thickness = 0;  ///< Material thickness.
    double front = 0;      ///< Reference-frame y of the plinth front face.
    double floor = 0;      ///< Reference-frame z of the floor.
    double left = 0;       ///< Reference-frame x of the run's left end.
    double length = 0;     ///< Run length (left end to right end), appliance end panels included.
    double usableStockLength = 0;
    /// Front piece extents [x0, x1] from the run's left end, left to right.
    std::vector<std::pair<double, double>> pieces;
    std::vector<std::string> runOrder; ///< Cabinet ids sorted left to right.
    std::string materialId;
    /// Findings that do not refuse the request: project.plinth.gap (Warning, a gap between two
    /// cabinets is bridged), project.plinth.front_offset (Info, carcass fronts not flush),
    /// project.plinth.appliance (Info, piece in front of an appliance gap; rule of thumb),
    /// project.plinth.butt_joint (Info, one per joint within a segment).
    core::Diagnostics notes;
};

/// Plans the plinth without changing the project. Errors (InvalidArgument / NotFound /
/// AlreadyExists, naming the item and numbers): the run errors of the worktop builder, a
/// cabinet with SidesToFloor or no plinth, Feet cabinets with different plinth heights, cabinets
/// on different floor levels, options out of range, an unknown material, a height wider than
/// the usable stock width, a return at an end closed by an appliance end panel, nothing left to
/// cover, an item id already in use.
core::Result<PlinthPlan> planPlinth(const Project& project, const PlinthRequest& request);

/// planPlinth, then appends the items. On error the project is unchanged.
core::Result<PlinthPlan> addPlinth(Project& project, const PlinthRequest& request);

namespace commands {
/// Undoable addPlinth (label "Add plinth {0} under {1}"): Affected = the new items.
Command addPlinth(PlinthRequest request);
} // namespace commands

} // namespace fos::project
