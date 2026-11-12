// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/domain/fixtures.hpp"
#include "fos/formats/atomic_file.hpp"
#include "fos/formats/codec.hpp"
#include "fos/formats/enums.hpp"
#include "model_equality.hpp"
#include "test_support.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace fos;
using namespace fos::formats;
using core::ErrorCode;
using formats::test::expectEqual;
using ::testing::HasSubstr;

namespace {

/// A model that uses every field, enumerator family and operation type with
/// non-default, awkward values (fractions that are not exact in binary, UTF-8 text).
domain::Model richModel() {
    domain::Model m = domain::fixtures::basicCabinet();

    domain::Material& mat = m.materials[0];
    mat.manufacturer = "Egger";
    mat.code = "H1180 ST37";
    mat.supplier = "Склад №1";
    mat.kind = domain::MaterialKind::SolidWood;
    mat.thickness = 18.3;
    mat.sheetLength = 2800.1;
    mat.sheetWidth = 2070.7;
    mat.color = {0, 255, 16};
    mat.texture = "textures/oak \"natural\".png";
    mat.costPerSheet = 61.99;
    mat.available = false;
    mat.custom = {{"decor", "Halifax"}, {"json:not-json", "{oops"}, {"note", "line1\nline2"}};

    domain::EdgeBand& band = m.edgeBands[0];
    band.manufacturer = "Rehau";
    band.code = "1234";
    band.kind = domain::EdgeBandKind::Veneer;
    band.thickness = 0.1 + 0.2; // 0.30000000000000004: must survive exactly
    band.width = 23.0;
    band.color = {1, 2, 3};
    band.custom = {{"glue", "EVA"}};

    domain::HardwareDefinition& hw = m.hardwareCatalog[0];
    hw.manufacturer = "Generic";
    hw.article = "SP-5";
    hw.dimensionsVerified = true;
    hw.params["offset"] = -1.0 / 3.0;
    hw.custom = {{"finish", "nickel"}};

    domain::Assembly& a = m.assemblies[0];
    a.placement = geometry::Transform::translation({1000.5, -200.25, 0}) * geometry::Transform::rotationZ(30.0);

    domain::Part& part = a.parts[2]; // bottom
    part.edge(domain::EdgeSide::L2) = domain::EdgeBandId("eb.abs04");
    part.edge(domain::EdgeSide::W1) = domain::EdgeBandId("eb.abs2");
    part.edge(domain::EdgeSide::W2) = domain::EdgeBandId("eb.abs2");
    part.grain = domain::GrainDirection::AlongWidth;
    part.role = domain::PartRole::DrawerBottom;
    part.custom = {{"label", "B-01"}};
    domain::Operation groove;
    groove.id = domain::OperationId("cab1/bottom/op.groove");
    groove.shape = domain::Groove{domain::Face::A, 0.0, 547.5, 764.0, 547.5, 4.2, 8.0};
    groove.source = "rule/back-groove";
    groove.note = "for the back panel";
    domain::Operation pocket;
    pocket.id = domain::OperationId("cab1/bottom/op.pocket");
    pocket.shape = domain::Pocket{domain::Face::L1, {{10, 2}, {60.5, 2}, {60.5, 16}, {10, 16}}, 12.5, false};
    domain::Operation cutout;
    cutout.id = domain::OperationId("cab1/bottom/op.cutout");
    cutout.shape = domain::Pocket{domain::Face::B, {{100, 100}, {200, 100}, {150, 180}}, 0.0, true};
    domain::Operation through;
    through.id = domain::OperationId("cab1/bottom/op.cable");
    through.shape = domain::Hole{domain::Face::W2, 280.0, 9.0, 60.0, 0.0, true};
    part.operations = {groove, pocket, cutout, through};

    a.hardware[0].position = {18.0, 37.0, 346.125};
    return m;
}

/// Encode, write as text, parse the text again and decode: the path a real file takes.
core::Result<domain::Model> throughText(const domain::Model& model) {
    const std::string text = toJson(model).dump(2);
    return modelFromJson(Json::parse(text));
}

} // namespace

// ---------------------------------------------------------------- round trips

TEST(CodecRoundTrip, BasicCabinetFixtureIsLossless) {
    const domain::Model original = domain::fixtures::basicCabinet();
    auto decoded = throughText(original);
    ASSERT_TRUE(decoded.ok()) << decoded.error().message;
    expectEqual(original, decoded.value());
    // Encoding is deterministic and stable across a round trip.
    EXPECT_EQ(toJson(decoded.value()).dump(), toJson(original).dump());
}

TEST(CodecRoundTrip, ModelUsingEveryFieldIsLossless) {
    const domain::Model original = richModel();
    auto decoded = throughText(original);
    ASSERT_TRUE(decoded.ok()) << decoded.error().message;
    expectEqual(original, decoded.value());
    EXPECT_EQ(decoded.value().edgeBands[0].thickness, 0.30000000000000004);
    EXPECT_TRUE(modelToJsonChecked(original).ok());
}

TEST(CodecRoundTrip, ModelSurvivesAContainerFileOnDisk) {
    formats::test::TempDir dir;
    FormatSpec spec;
    spec.kind = "test-model";
    Document doc;
    doc.format = "test-model";
    doc.payload["model"] = toJson(richModel());
    ASSERT_TRUE(saveDocument(dir / "model.json", doc).ok());
    auto loaded = loadDocument(dir / "model.json", spec);
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    auto decoded = modelFromJson(loaded.value().document.payload["model"], "/payload/model");
    ASSERT_TRUE(decoded.ok()) << decoded.error().message;
    expectEqual(richModel(), decoded.value());
}

TEST(CodecRoundTrip, EncodedFixtureHasTheDocumentedShape) {
    const Json j = toJson(domain::fixtures::basicCabinet());
    EXPECT_EQ(j["materials"][0]["id"], "mat.pb18");
    EXPECT_EQ(j["materials"][0]["kind"], "particleboard");
    EXPECT_EQ(j["materials"][0]["color"], "#DECCAA"); // 222, 204, 170
    EXPECT_EQ(j["materials"][1]["kind"], "hdf");
    EXPECT_EQ(j["edgeBands"][1]["thickness"], 0.4);
    EXPECT_EQ(j["hardwareCatalog"][0]["kind"], "shelf_support");
    EXPECT_EQ(j["hardwareCatalog"][0]["params"]["hole_depth"], 13.0);
    const Json& left = j["assemblies"][0]["parts"][0];
    EXPECT_EQ(left["id"], "cab1/side.left");
    EXPECT_EQ(left["role"], "side");
    EXPECT_EQ(left["grain"], "along_length");
    EXPECT_EQ(left["edges"], Json::parse(R"({"l1": "eb.abs2", "w1": "eb.abs04", "w2": "eb.abs04"})"));
    // Left side: length -> +Z, width -> +Y, thickness -> -X, origin (18, 0, 0).
    EXPECT_EQ(left["placement"],
              Json::parse(R"({"origin": [18.0, 0.0, 0.0], "xAxis": [0.0, 0.0, 1.0], "yAxis": [0.0, 1.0, 0.0], "zAxis": [-1.0, 0.0, 0.0]})"));
    EXPECT_EQ(left["operations"][0],
              Json::parse(R"({"id": "cab1/side.left/op.pin.front", "type": "hole", "face": "b", "u": 346.0, "v": 37.0,
                              "diameter": 5.0, "depth": 13.0, "through": false, "source": "cab1/hw.shelf_pins", "note": ""})"));
    EXPECT_EQ(j["assemblies"][0]["hardware"][0]["quantity"], 4);
    EXPECT_FALSE(left.contains("custom")); // empty custom map is omitted
}

// ---------------------------------------------------------------- enum tokens

TEST(CodecEnums, TokensAreTheDocumentedStableStrings) {
    const auto tokens = [](auto table) {
        std::vector<std::string> out;
        for (const auto& entry : table) out.emplace_back(entry.token);
        return out;
    };
    using V = std::vector<std::string>;
    EXPECT_EQ(tokens(enumTokens<domain::MaterialKind>()),
              (V{"particleboard", "mdf", "hdf", "plywood", "solid_wood", "other"}));
    EXPECT_EQ(tokens(enumTokens<domain::EdgeBandKind>()), (V{"abs", "pvc", "melamine", "veneer", "solid_wood", "other"}));
    EXPECT_EQ(tokens(enumTokens<domain::HardwareKind>()),
              (V{"hinge", "mounting_plate", "drawer_slide", "handle", "shelf_support", "confirmat", "dowel",
                 "cam_connector", "screw", "foot", "leg", "suspension", "sliding_system", "lift_mechanism", "other"}));
    EXPECT_EQ(tokens(enumTokens<domain::PartRole>()),
              (V{"side", "top", "bottom", "shelf", "partition", "back", "door", "drawer_front", "drawer_side",
                 "drawer_back", "drawer_bottom", "plinth", "worktop", "rail", "generic"}));
    EXPECT_EQ(tokens(enumTokens<domain::EdgeSide>()), (V{"l1", "l2", "w1", "w2"}));
    EXPECT_EQ(tokens(enumTokens<domain::GrainDirection>()), (V{"none", "along_length", "along_width"}));
    EXPECT_EQ(tokens(enumTokens<domain::Face>()), (V{"a", "b", "l1", "l2", "w1", "w2"}));

    EXPECT_EQ(enumToToken(domain::HardwareKind::CamConnector), "cam_connector");
    EXPECT_EQ(enumFromToken<domain::PartRole>("drawer_front"), domain::PartRole::DrawerFront);
    EXPECT_EQ(enumFromToken<domain::PartRole>("Drawer_Front"), std::nullopt); // case-sensitive
    EXPECT_EQ(enumToToken(static_cast<domain::Face>(99)), "");
    // Every table covers every enumerator exactly once and maps back.
    for (const auto& entry : enumTokens<domain::HardwareKind>())
        EXPECT_EQ(enumFromToken<domain::HardwareKind>(entry.token), entry.value);
    EXPECT_EQ(enumTokens<domain::HardwareKind>().size(), static_cast<std::size_t>(domain::HardwareKind::Other) + 1);
    EXPECT_EQ(enumTokens<domain::PartRole>().size(), static_cast<std::size_t>(domain::PartRole::Generic) + 1);
}

// ---------------------------------------------------------------- validation errors

namespace {
/// Decodes the fixture after applying `mutate` to its JSON; returns the error message.
template <class Mutate> core::Error modelError(Mutate mutate) {
    Json j = toJson(domain::fixtures::basicCabinet());
    mutate(j);
    auto r = modelFromJson(j);
    EXPECT_FALSE(r.ok());
    return r.ok() ? core::Error{} : r.error();
}
} // namespace

TEST(CodecErrors, NegativeLengthIsReportedWithItsPath) {
    const core::Error e = modelError([](Json& j) { j["assemblies"][0]["parts"][3]["length"] = -5; });
    EXPECT_EQ(e.code, ErrorCode::ParseError);
    EXPECT_EQ(e.message, "/assemblies/0/parts/3/length: expected a positive number, got -5");
    EXPECT_EQ(e.context, "/assemblies/0/parts/3/length");
}

TEST(CodecErrors, PathPrefixIsApplied) {
    Json j = toJson(domain::fixtures::basicCabinet());
    j["materials"][1]["thickness"] = 0;
    EXPECT_EQ(modelFromJson(j, "/payload/model").error().message,
              "/payload/model/materials/1/thickness: expected a positive number, got 0");
    EXPECT_EQ(modelFromJson(Json::array(), "").error().message, "/: expected a model (an object), got an array");
}

TEST(CodecErrors, WrongTypesAndMissingKeys) {
    EXPECT_EQ(modelError([](Json& j) { j["materials"][0]["thickness"] = "18"; }).message,
              "/materials/0/thickness: expected a positive number, got \"18\"");
    EXPECT_EQ(modelError([](Json& j) { j["materials"][0].erase("thickness"); }).message,
              "/materials/0/thickness: required key is missing");
    EXPECT_EQ(modelError([](Json& j) { j["materials"][0]["id"] = ""; }).message,
              "/materials/0/id: expected a non-empty string, got \"\"");
    EXPECT_EQ(modelError([](Json& j) { j["materials"][0]["hasGrain"] = 1; }).message,
              "/materials/0/hasGrain: expected true or false, got 1");
    EXPECT_EQ(modelError([](Json& j) { j["materials"][0]["costPerSheet"] = -0.5; }).message,
              "/materials/0/costPerSheet: expected a non-negative number, got -0.5");
    EXPECT_EQ(modelError([](Json& j) { j["materials"] = Json::object(); }).message,
              "/materials: expected an array, got an object");
    EXPECT_EQ(modelError([](Json& j) { j["materials"][1] = nullptr; }).message,
              "/materials/1: expected a material (an object), got null");
    EXPECT_EQ(modelError([](Json& j) { j["edgeBands"][0]["width"] = Json::array(); }).message,
              "/edgeBands/0/width: expected a positive number, got an array");
    EXPECT_EQ(modelError([](Json& j) { j["hardwareCatalog"][0]["params"]["hole_depth"] = "deep"; }).message,
              "/hardwareCatalog/0/params/hole_depth: expected a number, got \"deep\"");
    EXPECT_EQ(modelError([](Json& j) { j["assemblies"][0]["hardware"][0]["quantity"] = 0; }).message,
              "/assemblies/0/hardware/0/quantity: expected a whole number >= 1, got 0");
    EXPECT_EQ(modelError([](Json& j) { j["assemblies"][0]["hardware"][0]["quantity"] = 2.5; }).message,
              "/assemblies/0/hardware/0/quantity: expected a whole number >= 1, got 2.5");
    EXPECT_EQ(modelError([](Json& j) { j["assemblies"][0]["hardware"][0]["position"] = Json::array({1, 2}); }).message,
              "/assemblies/0/hardware/0/position: expected an array of 3 numbers [x, y, z], got an array of 2 elements");
    EXPECT_EQ(modelError([](Json& j) { j["assemblies"][0]["hardware"][0]["parts"][1] = 7; }).message,
              "/assemblies/0/hardware/0/parts/1: expected a string, got 7");
    EXPECT_EQ(modelError([](Json& j) { j["assemblies"][0]["parts"][0]["custom"]["k"] = 5; }).message,
              "/assemblies/0/parts/0/custom/k: expected a string, got 5");
}

TEST(CodecErrors, UnknownEnumValuesListTheAllowedTokens) {
    EXPECT_EQ(modelError([](Json& j) { j["materials"][0]["kind"] = "Particleboard"; }).message,
              "/materials/0/kind: expected one of \"particleboard\", \"mdf\", \"hdf\", \"plywood\", \"solid_wood\", "
              "\"other\", got \"Particleboard\"");
    EXPECT_EQ(modelError([](Json& j) { j["assemblies"][0]["parts"][0]["grain"] = "diagonal"; }).message,
              "/assemblies/0/parts/0/grain: expected one of \"none\", \"along_length\", \"along_width\", got \"diagonal\"");
    EXPECT_EQ(modelError([](Json& j) { j["assemblies"][0]["parts"][0]["operations"][1]["face"] = "c"; }).message,
              "/assemblies/0/parts/0/operations/1/face: expected one of \"a\", \"b\", \"l1\", \"l2\", \"w1\", \"w2\", got \"c\"");
    EXPECT_EQ(modelError([](Json& j) { j["assemblies"][0]["parts"][0]["operations"][0]["type"] = "drill"; }).message,
              "/assemblies/0/parts/0/operations/0/type: expected one of \"hole\", \"groove\", \"pocket\", got \"drill\"");
    EXPECT_EQ(modelError([](Json& j) { j["assemblies"][0]["parts"][0]["edges"]["l3"] = "eb.abs2"; }).message,
              "/assemblies/0/parts/0/edges/l3: unknown edge (allowed keys here: \"l1\", \"l2\", \"w1\", \"w2\")");
    EXPECT_EQ(modelError([](Json& j) { j["hardwareCatalog"][0]["kind"] = 3; }).code, ErrorCode::ParseError);
}

TEST(CodecErrors, ColorMustBeSixHexDigits) {
    EXPECT_EQ(colorFromJson(Json("#0a1B2c")).value(), (domain::Color{0x0a, 0x1b, 0x2c}));
    EXPECT_EQ(toJson(domain::Color{0x0a, 0x1b, 0x2c}), "#0A1B2C");
    for (const Json& bad : {Json("#12345"), Json("1234567"), Json("#12345G"), Json("#1234567"), Json("red"), Json(255),
                            Json("#12 456")}) {
        auto r = colorFromJson(bad, "/materials/0/color");
        ASSERT_FALSE(r.ok()) << bad.dump();
        EXPECT_THAT(r.error().message, HasSubstr("/materials/0/color: expected a colour string \"#RRGGBB\""));
    }
}

TEST(CodecErrors, PlacementMustBeARigidRightHandedFrame) {
    // Mirrored frame (zAxis flipped): a left-handed basis is not representable.
    core::Error mirrored = modelError([](Json& j) {
        j["assemblies"][0]["parts"][2]["placement"]["zAxis"] = Json::array({0, 0, -1});
    });
    EXPECT_THAT(mirrored.message, HasSubstr("/assemblies/0/parts/2/placement: the axes"));
    EXPECT_THAT(mirrored.message, HasSubstr("not a proper rotation"));
    // Scaled axis (length 2).
    EXPECT_THAT(modelError([](Json& j) { j["assemblies"][0]["placement"]["xAxis"] = Json::array({2, 0, 0}); }).message,
                HasSubstr("/assemblies/0/placement: the axes"));
    // Skewed axes: x not perpendicular to y.
    Json skew = Json::parse(R"({"origin": [0, 0, 0], "xAxis": [1, 0, 0], "yAxis": [0.6, 0.8, 0], "zAxis": [0, 0, 1]})");
    EXPECT_FALSE(transformFromJson(skew).ok());
    // A genuine rotation by 30 degrees is accepted and reproduced exactly.
    const auto rotated = geometry::Transform::rotationZ(30.0) ;
    auto back = transformFromJson(Json::parse(toJson(rotated).dump()));
    ASSERT_TRUE(back.ok()) << back.error().message;
    formats::test::expectEqual(rotated, back.value(), "rotationZ(30)");
    // Missing or unknown keys.
    EXPECT_EQ(modelError([](Json& j) { j["assemblies"][0]["placement"].erase("origin"); }).message,
              "/assemblies/0/placement/origin: required key is missing");
    EXPECT_THAT(modelError([](Json& j) { j["assemblies"][0]["placement"]["scale"] = 2; }).message,
                HasSubstr("/assemblies/0/placement/scale: unknown key (allowed keys here: \"origin\", \"xAxis\", \"yAxis\", \"zAxis\")"));
}

TEST(CodecErrors, OperationShapeValidation) {
    const auto op = [](const char* text) { return operationFromJson(Json::parse(text), "/op"); };
    EXPECT_EQ(op(R"({"id": "o", "type": "hole", "face": "a", "u": 1, "v": 2, "diameter": 0})").error().message,
              "/op/diameter: expected a positive number, got 0");
    EXPECT_EQ(op(R"({"id": "o", "type": "hole", "face": "a", "u": 1, "v": 2, "diameter": 5, "depth": -1})").error().message,
              "/op/depth: expected a non-negative number, got -1");
    EXPECT_EQ(op(R"({"id": "o", "type": "hole", "face": "a", "v": 2, "diameter": 5})").error().message,
              "/op/u: required key is missing");
    EXPECT_EQ(op(R"({"id": "o", "face": "a"})").error().message, "/op/type: required key is missing");
    EXPECT_EQ(op(R"({"id": "o", "type": "groove", "face": "b", "u1": 0, "v1": 0, "u2": 9, "v2": 0, "width": 4, "depth": 0})")
                  .error().message,
              "/op/depth: expected a positive number, got 0");
    EXPECT_EQ(op(R"({"id": "o", "type": "pocket", "face": "a", "contour": [[0, 0], [1, 0]], "depth": 3})").error().message,
              "/op/contour: expected at least 3 points [u, v], got 2");
    EXPECT_EQ(op(R"({"id": "o", "type": "pocket", "face": "a", "contour": [[0, 0], [1, 0], [1, "x"]], "depth": 3})")
                  .error().message,
              "/op/contour/2/1: expected a number, got \"x\"");
    // Keys of another shape are unknown for this shape and rejected, never dropped.
    EXPECT_THAT(op(R"({"id": "o", "type": "hole", "face": "a", "u": 1, "v": 2, "diameter": 5, "width": 4})").error().message,
                HasSubstr("/op/width: unknown key (allowed keys here: \"id\", \"type\", \"face\", \"u\", \"v\", "
                          "\"diameter\", \"depth\", \"through\", \"source\", \"note\")"));
    // Minimal valid hole: defaults depth 0, through false, source "user".
    auto minimal = op(R"({"id": "o", "type": "hole", "face": "l2", "u": 1, "v": 2, "diameter": 5, "through": true})");
    ASSERT_TRUE(minimal.ok()) << minimal.error().message;
    EXPECT_EQ(std::get<domain::Hole>(minimal.value().shape), (domain::Hole{domain::Face::L2, 1, 2, 5, 0, true}));
    EXPECT_EQ(minimal.value().source, "user");
}

TEST(CodecErrors, DuplicateIdsAreRejected) {
    EXPECT_EQ(modelError([](Json& j) { j["materials"][1]["id"] = "mat.pb18"; }).message,
              "/materials/1/id: duplicate id \"mat.pb18\" (already used by /materials/0)");
    EXPECT_EQ(modelError([](Json& j) { j["assemblies"][0]["parts"][4]["id"] = "cab1/bottom"; }).message,
              "/assemblies/0/parts/4/id: duplicate id \"cab1/bottom\" (already used by /assemblies/0/parts/2)");
    EXPECT_THAT(modelError([](Json& j) {
                    j["assemblies"][0]["parts"][0]["operations"][1]["id"] = "cab1/side.left/op.pin.front";
                }).message,
                HasSubstr("/assemblies/0/parts/0/operations/1/id: duplicate id"));
    EXPECT_THAT(modelError([](Json& j) { j["assemblies"].push_back(j["assemblies"][0]); }).message,
                HasSubstr("/assemblies/1/id: duplicate id \"cab1\""));
}

TEST(CodecErrors, CheckedEncodingRefusesModelsThatWouldNotLoad) {
    domain::Model nan = domain::fixtures::basicCabinet();
    nan.assemblies[0].parts[1].width = std::numeric_limits<double>::quiet_NaN();
    auto r = modelToJsonChecked(nan);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::InvalidArgument);
    EXPECT_THAT(r.error().message, HasSubstr("would not load again: /assemblies/0/parts/1/width: expected a positive number"));

    domain::Model zero = domain::fixtures::basicCabinet();
    zero.assemblies[0].parts[0].thickness = 0;
    EXPECT_THAT(modelToJsonChecked(zero).error().message, HasSubstr("/assemblies/0/parts/0/thickness"));

    domain::Model badEnum = domain::fixtures::basicCabinet();
    badEnum.materials[0].kind = static_cast<domain::MaterialKind>(42);
    EXPECT_THAT(modelToJsonChecked(badEnum).error().message, HasSubstr("/materials/0/kind"));

    domain::Model sheared = domain::fixtures::basicCabinet();
    sheared.assemblies[0].parts[0].placement.xAxis = {1, 1, 0};
    EXPECT_THAT(modelToJsonChecked(sheared).error().message, HasSubstr("/assemblies/0/parts/0/placement"));
}

// ---------------------------------------------------------------- unknown keys

TEST(CodecUnknownKeys, PreservedInCustomAndReEmittedForTypesWithCustomMap) {
    Json j = toJson(domain::fixtures::basicCabinet());
    j["materials"][0]["density"] = 650;
    j["materials"][0]["certificates"] = Json::parse(R"({"fsc": true, "codes": ["E1", "CARB2"]})");
    j["edgeBands"][0]["laser"] = true;
    j["hardwareCatalog"][0]["cadFile"] = "pin.step";
    j["assemblies"][0]["parts"][0]["cncProgram"] = Json::parse(R"({"file": "left.mpr", "rev": 3})");
    j["assemblies"][0]["parts"][0]["custom"]["label"] = "L-01";

    auto decoded = modelFromJson(j);
    ASSERT_TRUE(decoded.ok()) << decoded.error().message;
    const domain::Model& m = decoded.value();
    // Stashed under the reserved prefix as compact JSON text.
    EXPECT_EQ(m.materials[0].custom.at("json:density"), "650");
    EXPECT_EQ(m.materials[0].custom.at("json:certificates"), R"({"fsc":true,"codes":["E1","CARB2"]})");
    EXPECT_EQ(m.edgeBands[0].custom.at("json:laser"), "true");
    EXPECT_EQ(m.hardwareCatalog[0].custom.at("json:cadFile"), "\"pin.step\"");
    EXPECT_EQ(m.assemblies[0].parts[0].custom.at("json:cncProgram"), R"({"file":"left.mpr","rev":3})");
    EXPECT_EQ(m.assemblies[0].parts[0].custom.at("label"), "L-01");

    // Saving writes them back as the original top-level keys with the original values.
    const Json again = toJson(m);
    EXPECT_EQ(again["materials"][0]["density"], 650);
    EXPECT_EQ(again["materials"][0]["certificates"], j["materials"][0]["certificates"]);
    EXPECT_EQ(again["edgeBands"][0]["laser"], true);
    EXPECT_EQ(again["hardwareCatalog"][0]["cadFile"], "pin.step");
    EXPECT_EQ(again["assemblies"][0]["parts"][0]["cncProgram"], j["assemblies"][0]["parts"][0]["cncProgram"]);
    EXPECT_EQ(again["assemblies"][0]["parts"][0]["custom"], Json::parse(R"({"label": "L-01"})"));
    EXPECT_FALSE(again["materials"][0].contains("custom"));
    // Nothing was lost or invented anywhere: the whole document is equal (object key
    // order aside, which nlohmann's comparison of sorted-key json ignores).
    EXPECT_EQ(nlohmann::json(again), nlohmann::json(j));
    // And a second round trip is stable.
    expectEqual(m, modelFromJson(again).value());
}

TEST(CodecUnknownKeys, ReservedPrefixEntriesThatCannotBeRestoredStayCustomProperties) {
    domain::Material m = domain::fixtures::basicCabinet().materials[0];
    m.custom["json:name"] = "\"shadow\""; // would collide with the known key "name"
    m.custom["json:broken"] = "{not json";
    m.custom["json:"] = "1";
    const Json j = toJson(m);
    EXPECT_EQ(j["name"], "Particleboard 18 mm");
    EXPECT_EQ(j["custom"], Json::parse(R"({"json:": "1", "json:broken": "{not json", "json:name": "\"shadow\""})"));
    auto back = materialFromJson(j);
    ASSERT_TRUE(back.ok()) << back.error().message;
    EXPECT_EQ(back.value().custom, m.custom);
}

TEST(CodecUnknownKeys, RejectedForTypesWithoutCustomMap) {
    EXPECT_EQ(modelError([](Json& j) { j["extra"] = 1; }).message,
              "/extra: unknown key (allowed keys here: \"materials\", \"edgeBands\", \"hardwareCatalog\", \"assemblies\")");
    EXPECT_THAT(modelError([](Json& j) { j["assemblies"][0]["color"] = "#FFFFFF"; }).message,
                HasSubstr("/assemblies/0/color: unknown key (allowed keys here: \"id\", \"name\", \"placement\", \"parts\", \"hardware\")"));
    EXPECT_THAT(modelError([](Json& j) { j["assemblies"][0]["hardware"][0]["torque"] = 2; }).message,
                HasSubstr("/assemblies/0/hardware/0/torque: unknown key"));
    EXPECT_THAT(modelError([](Json& j) { j["assemblies"][0]["parts"][0]["operations"][0]["feed"] = 3000; }).message,
                HasSubstr("/assemblies/0/parts/0/operations/0/feed: unknown key"));
}

TEST(CodecDefaults, OptionalKeysFallBackToDomainDefaults) {
    auto material = materialFromJson(Json::parse(R"({"id": "m", "name": "Board", "thickness": 16})"));
    ASSERT_TRUE(material.ok()) << material.error().message;
    EXPECT_EQ(material.value().kind, domain::MaterialKind::Particleboard);
    EXPECT_EQ(material.value().sheetLength, 2800.0);
    EXPECT_EQ(material.value().sheetWidth, 2070.0);
    EXPECT_FALSE(material.value().hasGrain);
    EXPECT_TRUE(material.value().available);
    EXPECT_EQ(material.value().color, (domain::Color{200, 200, 200}));

    auto part = partFromJson(Json::parse(R"({"id": "p", "name": "Panel", "length": 600, "width": 300, "thickness": 16,
                                             "edges": {"l1": "e", "l2": null}})"));
    ASSERT_TRUE(part.ok()) << part.error().message;
    EXPECT_EQ(part.value().role, domain::PartRole::Generic);
    EXPECT_EQ(part.value().grain, domain::GrainDirection::None);
    EXPECT_TRUE(part.value().material.empty());
    EXPECT_EQ(part.value().edge(domain::EdgeSide::L1), domain::EdgeBandId("e"));
    EXPECT_FALSE(part.value().edge(domain::EdgeSide::L2).has_value()); // null = raw edge
    EXPECT_TRUE(part.value().placement.isRigid());
    EXPECT_TRUE(geometry::approxEqual(part.value().placement, geometry::Transform::identity(), 0.0));

    auto model = modelFromJson(Json::object());
    ASSERT_TRUE(model.ok());
    EXPECT_TRUE(model.value().assemblies.empty());
}

// ---------------------------------------------------------------- damaged input

TEST(CodecRobustness, RandomlyDamagedModelFilesNeverCrash) {
    Document doc;
    doc.format = "test-model";
    doc.payload["model"] = toJson(richModel());
    const std::string valid = serializeDocument(doc).value();
    FormatSpec spec;
    spec.kind = "test-model";
    LoadOptions lenient;
    lenient.allowChecksumMismatch = true; // let damaged payloads reach the codecs

    // Deterministic pseudo-random damage (64-bit LCG, Knuth's MMIX constants): overwrite
    // 1-3 bytes with structural characters, digits, or arbitrary bytes.
    std::uint64_t state = 0x853c49e6748fea9bULL;
    const auto next = [&state] {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(state >> 33);
    };
    const std::string alphabet = "{}[]\",:-0159.eE tfn\\\n";
    int loaded = 0, decoded = 0;
    for (int round = 0; round < 3000; ++round) {
        std::string text = valid;
        const int edits = 1 + static_cast<int>(next() % 3);
        for (int e = 0; e < edits; ++e) {
            const std::size_t pos = next() % text.size();
            text[pos] = (next() % 4 == 0) ? static_cast<char>(next() & 0xff) : alphabet[next() % alphabet.size()];
        }
        auto document = parseDocument(text, spec, lenient);
        if (!document.ok()) {
            EXPECT_FALSE(document.error().message.empty());
            continue;
        }
        ++loaded;
        const Json& payload = document.value().document.payload;
        auto model = modelFromJson(payload.contains("model") ? payload["model"] : Json(), "/payload/model");
        if (model.ok()) {
            ++decoded;
            EXPECT_TRUE(modelToJsonChecked(model.value()).ok()); // whatever decodes can be saved again
        } else {
            EXPECT_THAT(model.error().message, ::testing::StartsWith("/payload/model"));
        }
    }
    // Sanity of the experiment itself: both outcomes occurred.
    EXPECT_GT(loaded, 100);
    EXPECT_LT(decoded, loaded);
}
