// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/production/cost.hpp"
#include "fos/production/cut_list.hpp"
#include "fos/production/labels.hpp"
#include "fos/production/part_list.hpp"
#include "fos/production/reports.hpp"

#include <map>

/// Conversion of reports into display tables. All captions come from `Localisation`
/// (English defaults) so the application can pass Ukrainian or any other strings;
/// no user-interface language is hard-coded in the logic. Numbers are formatted with
/// `core::formatLength` (locale-independent, '.' decimal separator, trailing zeros
/// stripped). Edge codes (L1, L2, W1, W2) and face codes (A, B, ...) are the
/// architecture's identifiers and are not translated.
namespace fos::production {

struct PartListHeaders {
    std::string number = "No.", assembly = "Assembly", partId = "Part ID", name = "Name", role = "Role",
                material = "Material", length = "Length", width = "Width", thickness = "Thickness",
                cutLength = "Cut length", cutWidth = "Cut width", grain = "Grain", edgeL1 = "Edge L1",
                edgeL2 = "Edge L2", edgeW1 = "Edge W1", edgeW2 = "Edge W2", operations = "Operations";
};
struct CutListHeaders {
    std::string material = "Material", thickness = "Thickness", cutLength = "Cut length",
                cutWidth = "Cut width", quantity = "Qty", length = "Finished length",
                width = "Finished width", grain = "Grain", rotationAllowed = "Rotation allowed",
                edgeL1 = "Edge L1", edgeL2 = "Edge L2", edgeW1 = "Edge W1", edgeW2 = "Edge W2",
                name = "Name", operations = "Operations", partNumbers = "Part numbers",
                partIds = "Part IDs";
};
struct MaterialHeaders {
    std::string material = "Material", thickness = "Thickness", parts = "Parts",
                finishedArea = "Finished area (m2)", cutArea = "Cut area (m2)",
                sheetLength = "Sheet length", sheetWidth = "Sheet width",
                sheetsLowerBound = "Sheets (area lower bound)", costPerSheet = "Cost per sheet",
                costAtLowerBound = "Cost at area lower bound", note = "Note";
};
struct EdgeBandingHeaders {
    std::string edgeBand = "Edge band", thickness = "Thickness", width = "Width", edges = "Edges",
                appliedLength = "Applied length (m)", overhangAllowance = "Overhang allowance (m)",
                wasteAllowance = "Waste allowance (m)", purchaseLength = "Purchase length (m)",
                costPerMeter = "Cost per m", cost = "Cost", note = "Note";
};
struct HardwareHeaders {
    std::string hardware = "Hardware", manufacturer = "Manufacturer", article = "Article",
                quantity = "Qty", unitCost = "Unit cost", total = "Total",
                dimensionsVerified = "Dimensions verified", note = "Note";
};
struct MachiningHeaders {
    std::string number = "No.", assembly = "Assembly", partId = "Part ID", name = "Name",
                holes = "Holes (count x diameter @ face)", holeCount = "Holes", grooves = "Grooves",
                grooveLength = "Groove length", pockets = "Pockets";
};
struct LabelHeaders {
    std::string number = "No.", project = "Project", assembly = "Assembly", name = "Name",
                partId = "Part ID", length = "Length", width = "Width", thickness = "Thickness",
                cutLength = "Cut length", cutWidth = "Cut width", material = "Material",
                edgeCode = "Edges (L1 L2 W1 W2)", quantity = "Qty index";
};
struct CostHeaders {
    std::string category = "Category", item = "Item", quantity = "Quantity", unit = "Unit",
                unitCost = "Unit cost", cost = "Cost", currency = "Currency", note = "Note";
};
struct SummaryHeaders {
    std::string assembly = "Assembly", parts = "Parts", hardware = "Hardware items",
                operations = "Operations", finishedArea = "Finished area (m2)", width = "Width",
                depth = "Depth", height = "Height";
};

/// Words that appear inside cells.
struct Terms {
    std::string yes = "yes", no = "no";
    std::string missing = "MISSING";          ///< Missing material / edge band reference.
    std::string unknownDefinition = "UNKNOWN DEFINITION";
    std::string unverified = "dimensions not verified";
    std::string priceUnknown = "price unknown";
    std::string noSheetSize = "no sheet size";
    std::string grainNone = "none", grainAlongLength = "along length", grainAlongWidth = "along width";
    std::string materials = "Materials", edgeBanding = "Edge banding", hardware = "Hardware";
    std::string subtotal = "Subtotal", total = "Total";
    std::string sheets = "sheets", meters = "m", pieces = "pcs";
    std::string areaLowerBound = "area lower bound - real sheet count comes from the cutting optimiser";
    std::string suppliedSheetCount = "supplied sheet count";
    std::string totalIsLowerBound = "lower bound (materials at area lower bound)";
    std::string totalIncomplete = "incomplete (some prices unknown)";
    /// Role captions; roles not listed fall back to `domain::toString(PartRole)`.
    std::map<domain::PartRole, std::string> roles;
};

struct Localisation {
    PartListHeaders partList;
    CutListHeaders cutList;
    MaterialHeaders materials;
    EdgeBandingHeaders edgeBanding;
    HardwareHeaders hardware;
    MachiningHeaders machining;
    LabelHeaders labels;
    CostHeaders cost;
    SummaryHeaders summary;
    Terms terms;
};

/// Decimal places per quantity kind (rounding half away from zero, zeros stripped).
struct NumberFormat {
    int length = core::kDisplayDecimals; ///< mm values.
    int area = 4;                        ///< m2 values.
    int meters = 3;                      ///< m values.
    int money = 2;
};

Table partListTable(const PartList& r, const Localisation& loc = {}, const NumberFormat& nf = {});
Table cutListTable(const CutList& r, const Localisation& loc = {}, const NumberFormat& nf = {});
Table materialTable(const MaterialReport& r, const Localisation& loc = {}, const NumberFormat& nf = {});
Table edgeBandingTable(const EdgeBandingReport& r, const Localisation& loc = {}, const NumberFormat& nf = {});
Table hardwareTable(const HardwareReport& r, const Localisation& loc = {}, const NumberFormat& nf = {});
Table machiningTable(const MachiningSummary& r, const Localisation& loc = {}, const NumberFormat& nf = {});
Table labelTable(const LabelSet& r, const Localisation& loc = {}, const NumberFormat& nf = {});
/// One line per cost item, a subtotal per category and a final total row.
Table costTable(const CostEstimate& r, const Localisation& loc = {}, const NumberFormat& nf = {});
/// One row per assembly (envelope W x D x H).
Table summaryTable(const ProjectSummary& r, const Localisation& loc = {}, const NumberFormat& nf = {});

} // namespace fos::production
