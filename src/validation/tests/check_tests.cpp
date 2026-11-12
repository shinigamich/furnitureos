// SPDX-License-Identifier: AGPL-3.0-only
/// Checks 1, 2, 5, 6 and 7 on the hand-written fixture and hand-mutated copies of it.
#include "test_support.hpp"

#include <cmath>
#include <limits>

namespace fos::validation::test {
namespace {
using domain::EdgeBandId;
using domain::Face;
using domain::GrainDirection;
using domain::Hole;
using domain::Operation;
using domain::OperationId;

TEST(Fixture, BasicCabinetIsClean) {
    const domain::Model m = domain::fixtures::basicCabinet();
    const ValidationReport r = validateModel(m);
    EXPECT_EQ(r.counts.errors, 0u) << toText(r);
    EXPECT_EQ(r.counts.warnings, 0u) << toText(r);
    // The only info: the generic shelf pin is not verified (one finding per definition).
    ASSERT_EQ(r.counts.infos, 1u) << toText(r);
    EXPECT_EQ(r.diagnostics[0].code, "validation.hardware.unverified_dimensions");
    EXPECT_EQ(r.diagnostics[0].entityId, "hw.shelf_pin_5");
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.stats.assemblies, 1u);
    EXPECT_EQ(r.stats.parts, 6u);
    EXPECT_EQ(r.stats.operations, 2u);
    EXPECT_EQ(r.stats.hardwareInstances, 1u);
    EXPECT_EQ(r.stats.checksRun, 7u);
    EXPECT_EQ(r.stats.possiblePartPairs, 15u); // 6 * 5 / 2
    // Every pair of the fixture merely touches (or is apart): none passes the strict AABB
    // overlap, so no exact test is needed at all.
    EXPECT_EQ(r.stats.partPairsTested, 0u);
    EXPECT_EQ(r.stats.matingHolePairs, 0u); // the two pin holes are on the same part
}

TEST(Registry, ChecksCanBeSwitchedOff) {
    EXPECT_EQ(availableChecks().size(), 7u);
    EXPECT_EQ(availableChecks()[2].name, "part-interference");
    domain::Model m = domain::fixtures::basicCabinet();
    part(m, "cab1/shelf.1").length = std::numeric_limits<double>::quiet_NaN();
    part(m, "cab1/top").material = domain::MaterialId("mat.ghost");
    ValidationOptions off;
    off.checkParts = off.checkEdgeBanding = off.checkPartInterference = off.checkAssemblyInterference = false;
    off.checkMachining = off.checkHardware = off.checkCompleteness = false;
    const ValidationReport r = validateModel(m, off);
    EXPECT_TRUE(r.diagnostics.empty()) << toText(r);
    EXPECT_EQ(r.stats.checksRun, 0u);
}

TEST(Parts, NanSizeAndDanglingIdsDoNotCrash) {
    domain::Model m = domain::fixtures::basicCabinet();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    part(m, "cab1/shelf.1").length = nan;
    part(m, "cab1/shelf.1").placement.origin.x = nan;
    part(m, "cab1/top").material = domain::MaterialId("mat.ghost");
    part(m, "cab1/bottom").edge(domain::EdgeSide::L2) = EdgeBandId("eb.ghost");
    m.assemblies[0].hardware[0].parts.push_back(domain::PartId("cab1/ghost"));
    const ValidationReport r = validateModel(m);
    EXPECT_EQ(only(r, "validation.part.invalid_size").message,
              "'Shelf' has an invalid size of NaN x 540 x 18 mm (length x width x thickness); all three must be "
              "finite and greater than 0.");
    EXPECT_EQ(only(r, "validation.part.non_rigid_placement").partId, "cab1/shelf.1");
    EXPECT_EQ(only(r, "validation.part.missing_material").message,
              "'Top' refers to material 'mat.ghost', which is not in the model's material library.");
    EXPECT_EQ(only(r, "validation.edge.missing_band").partId, "cab1/bottom");
    EXPECT_EQ(only(r, "validation.hardware.unknown_part").relatedIds, std::vector<std::string>{"cab1/ghost"});
    EXPECT_EQ(count(r, "validation.interference.parts"), 0u) << toText(r);
}

TEST(Parts, NanEverywhereDoesNotCrash) {
    domain::Model m = domain::fixtures::basicCabinet();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    m.assemblies.push_back(m.assemblies[0]);
    m.assemblies[1].id = domain::AssemblyId("cab2");
    m.assemblies[1].placement.origin = {nan, 0, 0};
    part(m, "cab1/back").placement.xAxis = {nan, nan, nan};
    part(m, "cab1/top").thickness = -1;
    std::get<Hole>(part(m, "cab1/side.left").operations[0].shape).u = nan;
    Operation g;
    g.id = OperationId("g");
    g.source = "x";
    g.shape = domain::Groove{Face::A, nan, 0, 10, nan, nan, 8};
    part(m, "cab1/bottom").operations.push_back(g);
    m.materials[0].sheetLength = nan;
    m.edgeBands[0].thickness = nan;
    m.assemblies[0].hardware[0].quantity = -3;
    const ValidationReport r = validateModel(m);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(count(r, "validation.assembly.non_rigid_placement"), 1u) << toText(r);
    EXPECT_EQ(count(r, "validation.edge.invalid_band"), 1u);
    EXPECT_EQ(count(r, "validation.completeness.material_sheet_size"), 1u);
    EXPECT_FALSE(toMarkdown(r).empty());
}

TEST(Parts, ThicknessMismatchNamesBothNumbers) {
    domain::Model m = domain::fixtures::basicCabinet();
    part(m, "cab1/shelf.1").thickness = 16;
    const ValidationReport r = validateModel(m);
    EXPECT_EQ(only(r, "validation.part.thickness_mismatch").message,
              "'Shelf' is 16 mm thick but its material 'mat.pb18' is 18 mm thick.");
    EXPECT_EQ(r.counts.errors, 1u) << toText(r);
}

TEST(Parts, MissingMaterialDuplicateIdsAndNonRigid) {
    domain::Model m = domain::fixtures::basicCabinet();
    part(m, "cab1/top").material = domain::MaterialId();
    domain::Part copy = part(m, "cab1/shelf.1");
    copy.placement.origin.z = 100; // apart from the original shelf
    m.assemblies[0].parts.push_back(copy);
    part(m, "cab1/side.left").placement.xAxis = {0, 0, 2}; // scaled, not a rotation
    domain::Assembly second = m.assemblies[0];
    second.name = "Copy";
    second.placement = geometry::Transform::translation({0, 2000, 0});
    m.assemblies.push_back(second);
    const ValidationReport r = validateModel(m);
    // Each assembly reports its own problems: two of each per-part finding.
    EXPECT_EQ(count(r, "validation.part.missing_material"), 2u);
    EXPECT_EQ(findingsWithCode(r, "validation.part.missing_material")[0].message, "'Top' has no material assigned.");
    EXPECT_EQ(count(r, "validation.part.duplicate_id"), 2u);
    EXPECT_EQ(findingsWithCode(r, "validation.part.duplicate_id")[0].message,
              "Part id 'cab1/shelf.1' is used by 2 parts in assembly 'Base cabinet 800'.");
    EXPECT_EQ(count(r, "validation.part.non_rigid_placement"), 2u);
    EXPECT_EQ(only(r, "validation.assembly.duplicate_id").message,
              "Assembly id 'cab1' is used by 2 assemblies (assembly 'Base cabinet 800', assembly 'Copy').");
    // The copies are 2000 mm apart in Y: no cross-assembly interference.
    EXPECT_EQ(count(r, "validation.interference.assemblies"), 0u) << toText(r);
}

TEST(Parts, TooSmallIsAWarning) {
    domain::Model m = libraryModel({board("tiny", 8, 560)});
    const ValidationReport r = validateModel(m);
    const Finding f = only(r, "validation.part.too_small");
    EXPECT_EQ(f.severity, core::Severity::Warning);
    EXPECT_EQ(f.message, "'Board tiny' measures only 8 x 560 mm; parts below 10 mm in length or width cannot be "
                         "machined safely.");
}

TEST(Parts, PartLongerThanSheet) {
    domain::Model m = domain::fixtures::basicCabinet();
    // 2900 finished; W1 + W2 carry 0.4 mm bands -> cut length 2900 - 0.8 = 2899.2 > 2800.
    // Cut width 560 - 2 (L1) = 558. Grain along the length: no rotation (and 2899.2 > 2070 anyway).
    part(m, "cab1/side.left").length = 2900;
    const ValidationReport r = validateModel(m);
    const Finding f = only(r, "validation.part.exceeds_sheet");
    EXPECT_EQ(f.message, "'Left side' has a cut size of 2899.2 x 558 mm, which does not fit the 2800 x 2070 mm sheet "
                         "of material 'mat.pb18' with the grain running along the part length (no rotation allowed).");
    EXPECT_EQ(r.counts.errors, 1u) << toText(r);
}

TEST(Parts, GrainBlocksRotation) {
    // 2100 x 600 on a 2800 x 2070 sheet: fits only with its length along the sheet length.
    domain::Part p = board("g", 2100, 600);
    p.grain = GrainDirection::AlongWidth; // the width must follow the sheet length: 2100 > 2070 across
    domain::Model m = libraryModel({p});
    ValidationReport r = validateModel(m);
    EXPECT_EQ(only(r, "validation.part.exceeds_sheet").message,
              "'Board g' has a cut size of 2100 x 600 mm, which does not fit the 2800 x 2070 mm sheet of material "
              "'mat.pb18' with the grain running along the part width (part must be rotated by 90 degrees).");
    m.assemblies[0].parts[0].grain = GrainDirection::AlongLength; // 2100 <= 2800, 600 <= 2070
    EXPECT_EQ(count(validateModel(m), "validation.part.exceeds_sheet"), 0u);
    m.assemblies[0].parts[0].grain = GrainDirection::AlongWidth;
    m.materials[0].hasGrain = false; // the part grain is irrelevant without a material grain
    EXPECT_EQ(count(validateModel(m), "validation.part.exceeds_sheet"), 0u);
    // Without grain a part longer than the sheet width but shorter than its length fits rotated:
    m.assemblies[0].parts[0].length = 600;
    m.assemblies[0].parts[0].width = 2100;
    m.assemblies[0].parts[0].grain = GrainDirection::None;
    EXPECT_EQ(count(validateModel(m), "validation.part.exceeds_sheet"), 0u);
}

TEST(EdgeBanding, BandNarrowerThanBoard) {
    domain::Model m = domain::fixtures::basicCabinet();
    m.edgeBands[0].width = 16; // eb.abs2 on L1 of both sides, bottom, top and shelf
    const ValidationReport r = validateModel(m);
    EXPECT_EQ(count(r, "validation.edge.band_too_narrow"), 5u);
    EXPECT_EQ(r.counts.errors, 5u) << toText(r);
    const auto shelf = findingsForEntity(r, "cab1/shelf.1");
    ASSERT_EQ(shelf.size(), 1u);
    EXPECT_EQ(shelf[0].message, "'Shelf' edge L1: edge band 'eb.abs2' is 16 mm wide but the board is 18 mm thick.");
    EXPECT_EQ(shelf[0].relatedIds, std::vector<std::string>{"eb.abs2"});
}

TEST(EdgeBanding, BandTooThickAndNonPositiveCut) {
    domain::Model m = libraryModel();
    domain::EdgeBand thick = m.edgeBands[0];
    thick.id = EdgeBandId("eb.abs6");
    thick.thickness = 6;
    m.edgeBands.push_back(thick);
    domain::Part narrow = board("n", 400, 12);
    narrow.edge(domain::EdgeSide::L1) = thick.id; // 6 > 0.25 * 12 = 3 -> warning
    domain::Part zero = board("z", 400, 12, geometry::Transform::translation({0, 100, 0}));
    zero.edge(domain::EdgeSide::L1) = thick.id; // cut width 12 - 6 - 6 = 0 -> error
    zero.edge(domain::EdgeSide::L2) = thick.id;
    m.assemblies[0].parts = {narrow, zero};
    const ValidationReport r = validateModel(m);
    EXPECT_EQ(count(r, "validation.edge.band_too_thick"), 3u) << toText(r);
    EXPECT_EQ(findingsForEntity(r, "n")[0].message,
              "'Board n' edge L1: edge band 'eb.abs6' is 6 mm thick, more than 25 % of the part width 12 mm.");
    EXPECT_EQ(only(r, "validation.edge.non_positive_cut_size").message,
              "'Board z' has a cut size of 400 x 0 mm after subtracting its edge bands from the finished size "
              "400 x 12 mm.");
    EXPECT_EQ(r.counts.errors, 1u) << toText(r);
}

/// Adds a confirmat-style pair on cab1: a 7 mm through hole in face A (outer face, x = 0) of
/// the left side and a 5 x 45 mm hole in edge W1 of the bottom (x = 18), same source.
domain::Model confirmatPair(double bottomU) {
    domain::Model m = domain::fixtures::basicCabinet();
    Operation s;
    s.id = OperationId("cab1/side.left/op.cf");
    s.source = "cab1/hw.cf.test";
    // Face A of the left side: u = local x (height), v = local y (depth). Local (9, 50, 18)
    // -> assembly (18 - 18, 50, 9) = (0, 50, 9); tool enters along -local z = +X, x 0..18.
    s.shape = Hole{Face::A, 9, 50, 7, 0, true};
    part(m, "cab1/side.left").operations.push_back(s);
    Operation b;
    b.id = OperationId("cab1/bottom/op.cf");
    b.source = "cab1/hw.cf.test";
    // Edge W1 of the bottom: u = local y, v = local z. (bottomU, 9) -> assembly (18, bottomU, 9),
    // drilled along +X, x 18..63.
    b.shape = Hole{Face::W1, bottomU, 9, 5, 45, false};
    part(m, "cab1/bottom").operations.push_back(b);
    return m;
}

TEST(Machining, CoaxialMatingHolesAreAccepted) {
    const ValidationReport r = validateModel(confirmatPair(50));
    EXPECT_EQ(r.stats.matingHolePairs, 1u);
    EXPECT_EQ(r.counts.errors, 0u) << toText(r);
    EXPECT_EQ(r.counts.warnings, 0u) << toText(r);
}

TEST(Machining, MatingHolesOffsetByOneAndAHalfMillimetres) {
    // Bottom hole at y = 51.5 instead of 50: axis distance sqrt(0^2 + 1.5^2) = 1.5 mm > 0.1.
    const ValidationReport r = validateModel(confirmatPair(51.5));
    EXPECT_EQ(r.stats.matingHolePairs, 1u);
    const Finding f = only(r, "validation.machining.mating_holes_not_coaxial");
    EXPECT_EQ(f.message, "Hole 'cab1/side.left/op.cf' on 'Left side' and hole 'cab1/bottom/op.cf' on 'Bottom' (both "
                         "from 'cab1/hw.cf.test') should be coaxial but their axes are 1.5 mm apart (tolerance 0.1 "
                         "mm).");
    EXPECT_EQ(f.partId, "cab1/side.left");
    EXPECT_EQ(r.counts.errors, 1u) << toText(r);
}

TEST(Machining, HolesFurtherApartThanTheSearchRadiusAreNotPaired) {
    // 50 -> 59: 9 mm >= matingSearchRadius 8 -> not a mating pair (no finding at all).
    const ValidationReport r = validateModel(confirmatPair(59));
    EXPECT_EQ(r.stats.matingHolePairs, 0u);
    EXPECT_EQ(r.counts.errors, 0u) << toText(r);
}

TEST(Machining, PerPartFindingsAreMerged) {
    domain::Model m = domain::fixtures::basicCabinet();
    // 20 mm deep blind hole in an 18 mm side.
    std::get<Hole>(part(m, "cab1/side.left").operations[0].shape).depth = 20;
    const ValidationReport r = validateModel(m);
    const Finding f = only(r, "machining.hole.too_deep");
    EXPECT_EQ(f.assemblyId, "cab1");
    EXPECT_EQ(f.partId, "cab1/side.left");
}

TEST(Hardware, UnknownDefinitionPartsAndQuantity) {
    domain::Model m = domain::fixtures::basicCabinet();
    domain::HardwareInstance& h = m.assemblies[0].hardware[0];
    h.definition = domain::HardwareId("hw.nope");
    h.quantity = 0;
    h.parts.push_back(domain::PartId("cab1/ghost"));
    const ValidationReport r = validateModel(m);
    EXPECT_EQ(only(r, "validation.hardware.unknown_definition").message,
              "Hardware instance 'cab1/hw.shelf_pins' refers to definition 'hw.nope', which is not in the model's "
              "hardware catalogue.");
    EXPECT_EQ(only(r, "validation.hardware.invalid_quantity").message,
              "Hardware instance 'cab1/hw.shelf_pins' has quantity 0; it must be at least 1.");
    EXPECT_EQ(only(r, "validation.hardware.unknown_part").message,
              "Hardware instance 'cab1/hw.shelf_pins' refers to part 'cab1/ghost', which is not in assembly 'Base "
              "cabinet 800'.");
    EXPECT_EQ(count(r, "validation.hardware.unverified_dimensions"), 0u); // the unknown one is not "used"
}

TEST(Hardware, UnverifiedIsOneInfoPerDefinition) {
    domain::Model m = domain::fixtures::basicCabinet();
    domain::HardwareInstance second = m.assemblies[0].hardware[0];
    second.id = domain::HardwareInstanceId("cab1/hw.more_pins");
    m.assemblies[0].hardware.push_back(second);
    const ValidationReport r = validateModel(m);
    const Finding f = only(r, "validation.hardware.unverified_dimensions");
    EXPECT_EQ(f.severity, core::Severity::Info);
    EXPECT_EQ(f.message, "Hardware 'Shelf support pin 5 mm (generic)' (hw.shelf_pin_5, used by 2 instance(s)) has "
                         "dimensions that were not verified against a manufacturer data sheet; check the drilling "
                         "before production.");
    m.hardwareCatalog[0].dimensionsVerified = true;
    EXPECT_EQ(count(validateModel(m), "validation.hardware.unverified_dimensions"), 0u);
}

TEST(Completeness, NamesEmptyAssembliesAndMaterials) {
    domain::Model m = domain::fixtures::basicCabinet();
    part(m, "cab1/shelf.1").name.clear();
    domain::Assembly empty;
    empty.id = domain::AssemblyId("empty");
    empty.name = "Nothing";
    m.assemblies.push_back(empty);
    m.materials[1].sheetWidth = 0;    // hdf3
    m.materials[1].costPerSheet = 0;  // hdf3 is used by the back -> info
    domain::Material unused = m.materials[0];
    unused.id = domain::MaterialId("mat.unused");
    unused.thickness = 0;
    unused.costPerSheet = 0; // not used by any part -> no cost info
    m.materials.push_back(unused);
    const ValidationReport r = validateModel(m);
    EXPECT_EQ(only(r, "validation.completeness.unnamed_part").message,
              "Part 'cab1/shelf.1' in assembly 'Base cabinet 800' has no name; labels and reports will show its id.");
    EXPECT_EQ(only(r, "validation.completeness.empty_assembly").message, "Assembly 'Nothing' contains no parts.");
    EXPECT_EQ(only(r, "validation.completeness.material_sheet_size").message,
              "Material 'mat.hdf3' has no usable sheet size (2800 x 0 mm); cutting plans and sheet-fit checks need a "
              "positive length and width.");
    EXPECT_EQ(only(r, "validation.completeness.material_thickness").entityId, "mat.unused");
    EXPECT_EQ(only(r, "validation.completeness.unknown_cost").entityId, "mat.hdf3");
    EXPECT_EQ(r.counts.errors, 2u) << toText(r);
    EXPECT_EQ(r.counts.warnings, 2u) << toText(r);
}

} // namespace
} // namespace fos::validation::test
