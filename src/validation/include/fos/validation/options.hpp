// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/machining/validation.hpp"

#include <vector>

namespace fos::validation {

/// One row of the door hinge-count table: doors whose hinged edge is up to `maxDoorHeight`
/// (mm) long need at least `hinges` hinges.
struct HingeHeightRule {
    double maxDoorHeight = 0;
    int hinges = 2;
};

/// Engineering rule checks (check 8, README "engineering"). EVERY rule here is a HEURISTIC
/// with generic default assumptions, not a structural calculation or a manufacturer
/// specification; findings are Warning or Info, never Error. Lengths in mm, masses in kg.
struct EngineeringOptions {
    /// validation.engineering.shelf_sag: mid-span deflection of PartRole::Shelf parts as a
    /// simply supported beam over their length under a uniform load,
    /// delta = 5 W L^3 / (384 E I), I = width * thickness^3 / 12, W = load * 9.81 N.
    bool shelfSag = true;
    /// Uniformly distributed load per shelf (generic default 15 kg, e.g. a row of books).
    double shelfLoadKg = 15.0;
    /// Add the shelf's own weight (material density, or the generic density table) to W.
    bool shelfSelfWeight = true;
    /// Sag limit = min(span / sagLimitSpanRatio, sagLimitMm); a term <= 0 is not used.
    double sagLimitSpanRatio = 300.0;
    double sagLimitMm = 2.0;

    /// validation.engineering.too_few_hinges: hinges on a door (sum of the quantities of
    /// Hinge instances that reference it) against max(table count, ceil(weight / doorKgPerHinge)).
    bool hingeCount = true;
    /// RULE OF THUMB, check the hinge maker's chart. Ascending maxDoorHeight.
    std::vector<HingeHeightRule> hingeHeightTable{{900, 2}, {1600, 3}, {2000, 4}, {2400, 5}};
    /// Door mass one hinge is assumed to carry (generic 5 kg); <= 0 disables the weight term.
    double doorKgPerHinge = 5.0;

    /// validation.engineering.drawer_* / slide_too_long: drawer box length (DrawerSide
    /// extent along Y) against the cabinet interior depth and the drawer slide length.
    bool drawerDepth = true;
    /// Clear space required behind a drawer box inside the cabinet (default 0).
    double drawerMinRearClearance = 0.0;

    /// validation.engineering.tipping_risk: assembly height / depth above `tippingRatio`
    /// (and height >= tippingMinHeight) without wall fixing hardware.
    bool tipping = true;
    double tippingRatio = 2.0;
    double tippingMinHeight = 1000.0;
};

/// Switches and thresholds of validateModel(). All lengths in mm, angles in degrees.
/// Every check can be switched off individually; see src/validation/README.md for the
/// diagnostic codes each check produces.
struct ValidationOptions {
    bool checkParts = true;                ///< validation.part.*, validation.assembly.*
    bool checkEdgeBanding = true;          ///< validation.edge.*
    bool checkPartInterference = true;     ///< validation.interference.parts / .outside_groove
    bool checkAssemblyInterference = true; ///< validation.interference.assemblies
    bool checkMachining = true;            ///< machining.* (merged) + validation.machining.*
    bool checkHardware = true;             ///< validation.hardware.*
    bool checkCompleteness = true;         ///< validation.completeness.*
    /// validation.engineering.* (heuristics, Warning/Info only). OFF by default: opt in
    /// (CLI: fos validate --engineering); each rule has its own switch in `engineering`.
    bool checkEngineering = false;

    /// A part whose finished length or width is below this is reported as implausibly small.
    double minPartSize = 10.0;
    /// An edge band thicker than this share of the part dimension perpendicular to the
    /// banded edge (width for L1/L2, length for W1/W2) is reported.
    double maxBandThicknessShare = 0.25;

    /// Two solids interfere when they overlap by MORE than this on every separating-axis
    /// candidate. Parts that touch, or overlap by up to this amount, are accepted.
    double interferenceTolerance = 0.01;
    /// Groove engagement (README, check 3): when two interfering, axis-aligned parts overlap
    /// inside the swept box of a Groove operation of one of them, the overlap may extend beyond
    /// that box by at most this much on every axis (press fit). Beyond it the pair is reported
    /// as validation.interference.outside_groove.
    double grooveFitTolerance = 0.1;
    /// Contoured parts (ADR-0008): when the boxes of two parts interfere and at least one of
    /// them has a non-rectangular shape, refine the result with the exact outline (README,
    /// check 3). false = box result only (conservative).
    bool exactShapeInterference = true;
    /// A banded arc of a contoured outline must not be bent tighter than this factor times
    /// the band thickness (radius of the band's inner face = substrate radius).
    /// GENERIC, UNVERIFIED rule of thumb: default 10 -> 20 mm for a 2 mm ABS band. Real
    /// minimum radii depend on band material, temperature and machine; set per workshop.
    /// <= 0 disables validation.edge.radius_too_small.
    double edgeBandMinBendRadiusFactor = 10.0;
    /// Severity of interference between different assemblies in the project frame.
    core::Severity assemblyInterferenceSeverity = core::Severity::Warning;

    /// Mating holes (see README, check 5) whose axes are further apart than this are reported.
    double coaxialTolerance = 0.1;
    /// Two holes of the same source on different parts are regarded as a mating pair only
    /// when their axes are closer than this (heuristic; keeps neighbouring holes of a
    /// 32 mm row apart).
    double matingSearchRadius = 8.0;
    /// ... and when the drilled segments are at most this far apart along the axis.
    double matingMaxAxialGap = 1.0;
    /// ... and when their axes are parallel within this angle.
    double matingParallelToleranceDeg = 1.0;

    /// Options forwarded to machining::validateOperations for every part.
    machining::ValidationOptions machining;
    /// Rules and assumptions of the engineering check (checkEngineering).
    EngineeringOptions engineering;
};

} // namespace fos::validation
