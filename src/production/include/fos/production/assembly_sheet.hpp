// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/production/common.hpp"

/// Assembly sheets: an ordered step list per assembly (item) for the person who assembles a
/// flat-pack delivery.
///
/// HONESTY NOTE: the order is GENERIC. It is derived only from the part roles and hardware kinds
/// (carcass -> back -> shelves/dividers -> drawer boxes -> fronts -> handles -> other), not from an
/// analysis of the actual joints or of the accessibility of fasteners. It is a reasonable default
/// for cabinet-style furniture and is printed with that caveat; it is not a verified instruction.
///
/// Step of a PART by role:
///   Carcass            Side, Top, Bottom, Rail, Plinth
///   Back               Back
///   ShelvesAndDividers Shelf, Partition
///   DrawerBoxes        DrawerSide, DrawerBack, DrawerBottom
///   Fronts             Door, DrawerFront
///   Other              Worktop, Generic
/// Step of a HARDWARE instance by kind:
///   Foot, Leg -> Carcass; ShelfSupport -> ShelvesAndDividers; DrawerSlide -> DrawerBoxes;
///   Hinge, MountingPlate, LiftMechanism, SlidingSystem -> Fronts; Handle -> Handles;
///   Suspension -> Other; fasteners and Other (Confirmat, Dowel, CamConnector, Screw, Other)
///   -> the LATEST step among the parts they connect (HardwareInstance::parts), or Other when
///   none of those parts is in the assembly.
/// Empty steps are omitted; the remaining steps are numbered 1..n.
namespace fos::production {

enum class AssemblyStepKind { Carcass, Back, ShelvesAndDividers, DrawerBoxes, Fronts, Handles, Other };

/// English step title ("Carcass", "Back panel", ...).
const char* toString(AssemblyStepKind kind) noexcept;
AssemblyStepKind stepForRole(domain::PartRole role) noexcept;

struct StepPart {
    int partNumber = 0; ///< PartRow::number.
    domain::PartId partId;
    std::string name;
    domain::PartRole role = domain::PartRole::Generic;
};

struct StepHardware {
    domain::HardwareId definition;
    std::string name; ///< The raw definition id when `unknownDefinition`.
    domain::HardwareKind kind = domain::HardwareKind::Other;
    bool unknownDefinition = false;
    int quantity = 0; ///< Sum of instance quantities assigned to the step.
};

struct AssemblyStep {
    int number = 0; ///< 1-based.
    AssemblyStepKind kind = AssemblyStepKind::Other;
    std::string title;
    std::string instruction;          ///< Generic English sentence for this kind of step.
    std::vector<StepPart> parts;      ///< Part-number order.
    std::vector<StepHardware> hardware; ///< Catalogue order, then unknown ids sorted.
};

struct AssemblySheet {
    domain::AssemblyId assemblyId;
    std::string assemblyName;
    std::vector<AssemblyStep> steps;
};

struct AssemblySheets {
    std::vector<AssemblySheet> sheets; ///< Model order; assemblies without parts or hardware omitted.
    /// production.hardware.invalid_quantity (instance skipped), production.hardware.unknown_definition.
    core::Diagnostics diagnostics;
};

/// The caveat printed on every sheet (see the honesty note above).
extern const char* const kAssemblyOrderNote;

AssemblySheets assemblySheets(const domain::Model& model, const Scope& scope = {});

/// Plain text of one sheet (title, caveat, numbered steps with parts and hardware counts).
std::string assemblySheetText(const AssemblySheet& sheet);

/// Standalone UTF-8 SVG of one sheet, A4 width (210 mm, 1 user unit = 1 mm); the height grows
/// with the number of lines (min. 297 mm). Text only: no exploded views are drawn.
std::string assemblySheetSvg(const AssemblySheet& sheet, const std::string& projectName);

} // namespace fos::production
