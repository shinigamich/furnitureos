// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/production/common.hpp"
#include "fos/production/labels.hpp"

#include <optional>

/// Packing lists for delivery / flat-pack: the panels of each assembly (item) grouped into
/// packages, plus one hardware bag per assembly.
///
/// WEIGHTS are never guessed. A panel's weight is
///     finished area (m2, domain shape area for contoured parts) x thickness (m) x density (kg/m3)
/// with `domain::Material::density`. When the material is missing or its density is 0 (unknown)
/// the panel's weight is UNKNOWN; such panels are packed separately from panels of known weight
/// (so that the weights of the other packages stay known) and their packages are flagged
/// "weight unknown". Package weights are the panels only: packaging material and the hardware
/// bag are not included (hardware definitions carry no weight).
///
/// PACKAGE SIZE: panels lie flat, stacked, each with its longer side along the package length.
///     package length = max(longer side) + 2 x padding
///     package width  = max(shorter side) + 2 x padding
///     package height = sum(thickness) + 2 x padding          (all mm, finished panel sizes)
///
/// PACKING RULE (a simple deterministic heuristic, not an optimiser): panels are sorted by longer
/// side, then shorter side (descending), then part number; each panel goes into the first package
/// of its weight class (known / unknown) where the weight limit (known class only) and the height
/// limit still hold, otherwise a new package is opened. A panel that alone exceeds the weight
/// limit or a size limit gets its own package flagged `overweight` / `oversize` with a warning.
namespace fos::production {

struct PackingOptions {
    /// Maximum panel weight per package, kg. Must be a positive finite number.
    double maxPackageWeightKg = 25.0;
    /// Maximum package length / width / height including padding, mm; 0 = no limit.
    double maxPackageLength = 0.0;
    double maxPackageWidth = 0.0;
    double maxPackageHeight = 0.0;
    /// Packaging allowance added on EACH side of the panel stack, mm (>= 0).
    double padding = 10.0;
};

/// One panel inside a package.
struct PackedPart {
    int partNumber = 0; ///< PartRow::number.
    domain::PartId partId;
    std::string name;
    domain::PartRole role = domain::PartRole::Generic;
    std::string materialName; ///< Raw material id when the material is missing.
    double length = 0, width = 0, thickness = 0; ///< Finished, mm.
    std::optional<double> weightKg;              ///< Empty = unknown (no density / no material).
};

struct Package {
    int number = 0;      ///< 1-based within its assembly.
    std::string id;      ///< "<assembly id>-P<number>".
    std::vector<PackedPart> parts; ///< Stacking order: bottom (largest) first.
    double length = 0, width = 0, height = 0; ///< Outer size incl. padding, mm.
    std::optional<double> weightKg; ///< Sum of the panel weights; empty when any is unknown.
    bool overweight = false; ///< A single panel heavier than the limit.
    bool oversize = false;   ///< A single panel larger than a size limit.
};

/// One line of a hardware bag: a hardware definition and its total count in the assembly.
struct BagLine {
    domain::HardwareId definition;
    std::string name;    ///< The raw definition id when `unknownDefinition`.
    std::string article;
    domain::HardwareKind kind = domain::HardwareKind::Other;
    bool unknownDefinition = false;
    int quantity = 0;
};

/// Hardware bag of one assembly. Fasteners = kinds Confirmat, Dowel, CamConnector, Screw;
/// every other kind is a fitting. Lines in hardware catalogue order, then unknown ids sorted.
struct HardwareBag {
    std::string id; ///< "<assembly id>-H".
    std::vector<BagLine> fittings, fasteners;
    bool empty() const noexcept { return fittings.empty() && fasteners.empty(); }
    int totalQuantity() const noexcept;
};

struct AssemblyPacking {
    domain::AssemblyId assemblyId;
    std::string assemblyName;
    std::vector<Package> packages; ///< Known-weight packages first, then unknown-weight ones.
    HardwareBag bag;
};

struct PackingList {
    PackingOptions options;
    std::vector<AssemblyPacking> assemblies; ///< Model order (assemblies without parts or hardware omitted).
    /// production.packing.density_unknown (one per material), production.material.missing,
    /// production.packing.invalid_size (panel skipped), production.packing.overweight,
    /// production.packing.oversize, production.packing.weight_unknown (shaped part with an
    /// invalid outline), plus the hardware diagnostics of hardwareReport().
    core::Diagnostics diagnostics;
};

/// True for the fastener kinds (Confirmat, Dowel, CamConnector, Screw).
bool isFastener(domain::HardwareKind kind) noexcept;

/// Packing list of the model (or one assembly). Error (InvalidArgument) for invalid options:
/// maxPackageWeightKg not positive finite, a size limit or the padding negative or not finite.
core::Result<PackingList> packingList(const domain::Model& model, const PackingOptions& options = {},
                                      const Scope& scope = {});

/// "12.35 kg" (2 decimals) or "weight unknown".
std::string weightText(const std::optional<double>& kg);

/// Plain-text packing list, one block per assembly (diagnostics are not included).
std::string packingListText(const PackingList& list);

/// One row per packed panel plus one row per hardware-bag line. Columns: Assembly, Package,
/// Package size (mm), Package weight (kg), No., Item, Material, L, W, T, Quantity, Weight (kg).
/// Unknown weights are the text "unknown".
Table packingTable(const PackingList& list);

/// Package labels (one per package, plus one per non-empty hardware bag) as SVG pages laid out
/// with the part-label sheet geometry (`LabelSheetOptions`, `labelGrid`). A label shows the
/// project, the assembly, "Package i/n" (or "Hardware bag"), the outer size, the weight (or
/// "WEIGHT UNKNOWN") and the part numbers it contains. Same SVG conventions as
/// renderLabelSheetsSvg (mm units, XML-escaped text clipped to the label).
core::Result<std::vector<std::string>> renderPackageLabelsSvg(const PackingList& list, const std::string& projectName,
                                                              const LabelSheetOptions& options = {});

} // namespace fos::production
