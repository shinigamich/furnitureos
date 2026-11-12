// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/diagnostic.hpp"
#include "fos/machining/validation.hpp"

namespace fos::validation {

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
};

} // namespace fos::validation
