// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/hardware/placement.hpp"

#include <limits>
#include <vector>

/// Drawer slides (kind DrawerSlide): sizing rules for the drawer box and the placement of
/// the mounting holes on the cabinet side and on the drawer-box side.
///
/// Two families are described by the parameters of a definition (catalog.hpp lists the keys
/// and the generic values):
///  * SIDE-MOUNTED (`undermount` = 0, e.g. ball-bearing slides): the slide sits in the gap
///    between box side and cabinet side; box outer width = opening − 2 · side_clearance.
///    Drawer-member holes are drilled into the OUTER face of the box side.
///  * UNDERMOUNT (`undermount` = 1, concealed runners): INNER-WIDTH RULE
///    box inner width = opening − inner_width_deduction, so
///    box outer width = opening − inner_width_deduction + 2 · side thickness,
///    with side thickness ≤ max_side_thickness. Drawer-member holes are pilot holes in the
///    LOWER EDGE of the box side (centred in its thickness); the box bottom must be raised
///    by at least `bottom_recess` above the box underside.
///
/// Hole positions along the slide are measured from the slide's FRONT end (the end with the
/// smaller assembly Y; for a slide running along assembly X, the smaller X): first, first +
/// pitch, ... while ≤ (length − hole_end_margin). The cabinet holes lie on a line
/// `hole_line_height` above the box underside (the lowest point of the drawer side); for
/// side-mounted slides the drawer holes lie on the same line on the box side.
/// The two hole rows are NOT required to be coaxial (positions come from the definition).
namespace fos::hardware {

/// True when the definition describes an undermount (concealed) runner (`undermount` = 1).
core::Result<bool> isUndermountSlide(const domain::HardwareDefinition& slide);

/// Nominal lengths length_min, length_min + length_step, ... ≤ length_max (ascending).
core::Result<std::vector<double>> slideNominalLengths(const domain::HardwareDefinition& slide);

/// Largest nominal length L with L + depth_allowance ≤ `availableDepth` (mm, the usable
/// depth behind the box front). ConstraintViolation naming the shortest slide and the depth
/// it needs when even that one does not fit.
core::Result<double> selectSlideLength(const domain::HardwareDefinition& slide, double availableDepth);

/// Outer width of the drawer box for an opening of `openingWidth` (clear width between the
/// two carrying panels) and box sides `sideThickness` thick (rules above). Errors: the result
/// is ≤ 2 · sideThickness, or (undermount) the sides are thicker than max_side_thickness.
core::Result<double> drawerBoxOuterWidth(const domain::HardwareDefinition& slide, double openingWidth,
                                         double sideThickness);

/// Box length (depth) for a slide of `slideLength`: slideLength − box_length_deduction.
core::Result<double> drawerBoxLength(const domain::HardwareDefinition& slide, double slideLength);

struct DrawerSlideConfig {
    domain::HardwareInstanceId instanceId; ///< Required.
    double slideLength = 0;                ///< Nominal length, mm (> 0), e.g. from selectSlideLength.
    /// Deepest blind hole allowed in the cabinet side (e.g. a partition carrying slides on
    /// both faces); the catalogue depth is reduced to it.
    double maxCabinetHoleDepth = std::numeric_limits<double>::infinity();
    /// Allowed deviation of the measured side gap from the rule's gap, mm.
    double gapTolerance = 0.5;
};

/// One slide between `drawerSide` (box side) and `cabinetSide` (cabinet side / partition),
/// both vertical panels whose large faces are parallel and normal to a horizontal axis.
/// Geometry is read from the placements (same assembly frame):
///  * the cabinet face is the face (A/B) of the cabinet side that looks at the drawer side;
///    the drawer face (side-mounted) the face of the drawer side that looks back;
///  * the clear gap between them must equal the rule's gap (side_clearance, or for
///    undermount (inner_width_deduction − 2 · drawer side thickness) / 2) ± gapTolerance;
///  * the slide runs from the drawer side's front end; it must end within the cabinet side.
/// Result: one instance (quantity 1, parts {cabinetSide, drawerSide}, position = slide front
/// end on the cabinet face at the hole line); operations: cabinet holes front to back, then
/// drawer holes front to back, ids `<instance>/op.N`.
/// Errors name the parts and numbers: slide length not a nominal length, parts not parallel
/// or vertical, wrong gap, slide longer than the cabinet side or the drawer side, hole deeper
/// than the panel, hole line outside a panel, missing / invalid parameters.
core::Result<Placement> placeDrawerSlides(const domain::Part& drawerSide, const domain::Part& cabinetSide,
                                          const domain::HardwareDefinition& slide, const DrawerSlideConfig& config);

} // namespace fos::hardware
