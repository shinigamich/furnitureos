// SPDX-License-Identifier: AGPL-3.0-only
// Part::shape (ADR-0008) in the JSON codec: round trips, schema, validation errors with paths.
#include "fos/formats/codec.hpp"
#include "fos/formats/container.hpp"
#include "fos/formats/atomic_file.hpp"
#include "fos/formats/sha256.hpp"
#include "model_equality.hpp"
#include "shape_fixtures.hpp"
#include "test_support.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace fos;
using namespace fos::formats;
using core::ErrorCode;
using formats::test::expectEqual;
using ::testing::HasSubstr;
using ::testing::StartsWith;

namespace {

const char* const kPartPath = "/assemblies/0/parts/0";

/// Decodes `part` at kPartPath and returns the error message ("" and a test failure on success).
/// Checks the error contract: ParseError, message "<context>: ...".
std::string partError(const Json& part) {
    auto decoded = partFromJson(part, kPartPath);
    EXPECT_FALSE(decoded.ok()) << "decoded although invalid: " << part.dump();
    if (decoded.ok()) return "";
    EXPECT_EQ(decoded.error().code, ErrorCode::ParseError);
    EXPECT_THAT(decoded.error().message, StartsWith(decoded.error().context + ": "));
    return decoded.error().message;
}

Json vertex(double x, double y, double bulge = 0) {
    Json v = Json::object();
    v["x"] = x;
    v["y"] = y;
    if (bulge != 0) v["bulge"] = bulge;
    return v;
}

Json squareJson(double x0, double y0, double size) {
    return Json::array({vertex(x0, y0), vertex(x0 + size, y0), vertex(x0 + size, y0 + size), vertex(x0, y0 + size)});
}

/// The worktop part as JSON, to be damaged by the error tests.
Json worktopJson() { return toJson(formats::test::worktop()); }

} // namespace

// ---------------------------------------------------------------- round trips

TEST(ShapeCodec, ExamplePartsRoundTripByteIdentically) {
    const domain::Model original = formats::test::shapedModel();
    const std::string text = toJson(original).dump(2);
    auto decoded = modelFromJson(Json::parse(text));
    ASSERT_TRUE(decoded.ok()) << decoded.error().message;
    expectEqual(original, decoded.value());
    EXPECT_EQ(toJson(decoded.value()).dump(2), text); // byte-identical second generation
    EXPECT_TRUE(modelToJsonChecked(original).ok());
    // The exact doubles survive (bulges of the rounded corners are tan(90/4 deg), not exact in binary).
    const auto& round = decoded.value().assemblies[1].parts[0];
    ASSERT_TRUE(round.shape.has_value());
    EXPECT_EQ(round.shape->outer, formats::test::roundedPanel().shape->outer);
    EXPECT_EQ(round.shape->outerEdges.size(), 8u);
    EXPECT_FALSE(round.shape->outerEdges[7].has_value());
}

TEST(ShapeCodec, ExamplePartsSurviveAContainerFileOnDisk) {
    formats::test::TempDir dir;
    FormatSpec spec;
    spec.kind = "test-model";
    Document doc;
    doc.format = "test-model";
    doc.savedAt = "2026-11-16T00:00:00Z";
    doc.payload["model"] = toJson(formats::test::shapedModel());
    ASSERT_TRUE(saveDocument(dir / "m.json", doc).ok());
    const std::string first = formats::test::readText(dir / "m.json");
    auto loaded = loadDocument(dir / "m.json", spec);
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    auto decoded = modelFromJson(loaded.value().document.payload["model"], "/payload/model");
    ASSERT_TRUE(decoded.ok()) << decoded.error().message;
    expectEqual(formats::test::shapedModel(), decoded.value());
    // Re-encode and save again: the file is byte-identical.
    Document again = loaded.value().document;
    again.payload["model"] = toJson(decoded.value());
    ASSERT_TRUE(saveDocument(dir / "m2.json", again).ok());
    EXPECT_EQ(formats::test::readText(dir / "m2.json"), first);
}

TEST(ShapeCodec, RectangularPartsAreEncodedExactlyAsBefore) {
    // Golden values recorded with the codec BEFORE shapes existed (same fixture, dump(2)).
    const std::string text = toJson(domain::fixtures::basicCabinet()).dump(2);
    EXPECT_EQ(text.size(), 7781u);
    EXPECT_EQ(sha256Hex(text), "5c0fe29435c76b70b6aba0ce7a5921718f8922d20af69b74d3e33c2b58d72133");
    const Json j = Json::parse(text);
    for (const auto& part : j["assemblies"][0]["parts"]) EXPECT_FALSE(part.contains("shape"));
    // Inside a model with shaped parts, the rectangular ones are unchanged as well.
    const Json mixed = toJson(formats::test::shapedModel());
    EXPECT_EQ(mixed["assemblies"][0], j["assemblies"][0]);
}

TEST(ShapeCodec, EncodedShapeHasTheDocumentedLayout) {
    const Json j = worktopJson();
    // Key order: ..., "operations", "shape" (custom map after it when present).
    std::vector<std::string> keys;
    for (const auto& item : j.items()) keys.push_back(item.key());
    EXPECT_EQ(keys.back(), "shape");
    EXPECT_EQ(j["edges"], Json::object());
    EXPECT_EQ(j["shape"], Json::parse(R"({
        "outer": [{"x": 0.0, "y": 0.0}, {"x": 1200.0, "y": 0.0}, {"x": 1200.0, "y": 600.0}, {"x": 0.0, "y": 600.0}],
        "cutouts": [
            [{"x": 350.0, "y": 100.0}, {"x": 850.0, "y": 100.0}, {"x": 850.0, "y": 500.0}, {"x": 350.0, "y": 500.0}],
            [{"x": 1030.0, "y": 300.0, "bulge": 1.0}, {"x": 970.0, "y": 300.0, "bulge": 1.0}]
        ],
        "outerEdges": ["eb.abs2", null, null, null]
    })"));
    // Arc-top side: the bulge is written only on the arc vertex.
    const Json arc = toJson(formats::test::arcTopSide())["shape"]["outer"];
    EXPECT_FALSE(arc[0].contains("bulge"));
    EXPECT_EQ(arc[2]["bulge"], 0.25);
    EXPECT_EQ(toJson(formats::test::arcTopSide())["shape"]["cutouts"], Json::array());
    EXPECT_EQ(toJson(formats::test::arcTopSide())["shape"]["outerEdges"], Json::array());
}

TEST(ShapeCodec, OptionalKeysAndExplicitZeroBulge) {
    // "bulge" may be omitted (0) or written as 0; "cutouts" and "outerEdges" may be omitted.
    Json part = toJson(formats::test::arcTopSide());
    part["shape"] = Json::parse(R"({"outer": [{"x": 0, "y": 0, "bulge": 0}, {"x": 800, "y": 0},
                                              {"x": 800, "y": 600, "bulge": 0.25}, {"x": 0, "y": 600}]})");
    auto decoded = partFromJson(part, kPartPath);
    ASSERT_TRUE(decoded.ok()) << decoded.error().message;
    EXPECT_EQ(decoded.value().shape, formats::test::arcTopSide().shape);
    EXPECT_FALSE(toJson(decoded.value())["shape"]["outer"][0].contains("bulge")); // normalised on save
}

TEST(ShapeCodec, ClockwiseCutoutIsAcceptedAndKeptAsWritten) {
    domain::Part part = formats::test::worktop();
    part.shape->cutouts[0] = geometry::reversed(part.shape->cutouts[0]);
    auto decoded = partFromJson(Json::parse(toJson(part).dump()), kPartPath);
    ASSERT_TRUE(decoded.ok()) << decoded.error().message;
    EXPECT_EQ(decoded.value().shape, part.shape);
}

TEST(ShapeCodec, BoundingBoxToleranceIsOneMicrometre) {
    // length 1200 + 5e-7: the outline's box differs by 5e-7 mm <= 1e-6 -> accepted.
    Json part = worktopJson();
    part["length"] = 1200.0000005;
    EXPECT_TRUE(partFromJson(part, kPartPath).ok());
    // 2e-6 mm too long -> rejected, naming both boxes.
    part["length"] = 1200.000002;
    const std::string message = partError(part);
    EXPECT_THAT(message, StartsWith("/assemblies/0/parts/0/shape/outer: the bounding box of the outer contour is "
                                    "[0, 1200] x [0, 600] mm"));
    EXPECT_THAT(message, HasSubstr("requires [0, 1200.000002] x [0, 600] (tolerance 0.000001 mm)"));
}

TEST(ShapeCodec, PreservedJsonShapeEntryOfARectangularPartStaysACustomProperty) {
    domain::Part part = domain::fixtures::basicCabinet().assemblies[0].parts[0];
    part.custom["json:shape"] = R"({"outer": []})";
    const Json j = toJson(part);
    EXPECT_FALSE(j.contains("shape")); // never turned into a real (and invalid) shape
    auto decoded = partFromJson(Json::parse(j.dump()), kPartPath);
    ASSERT_TRUE(decoded.ok()) << decoded.error().message;
    EXPECT_FALSE(decoded.value().shape.has_value());
    EXPECT_EQ(decoded.value().custom, part.custom);
}

// ---------------------------------------------------------------- structural errors

TEST(ShapeCodecErrors, StructureAndTypes) {
    Json part = worktopJson();
    part["shape"] = 5;
    EXPECT_EQ(partError(part), "/assemblies/0/parts/0/shape: expected a part shape (an object), got 5");

    part = worktopJson();
    part["shape"].erase("outer");
    EXPECT_THAT(partError(part), StartsWith("/assemblies/0/parts/0/shape/outer: "));

    part = worktopJson();
    part["shape"]["outer"] = "square";
    EXPECT_EQ(partError(part), "/assemblies/0/parts/0/shape/outer: expected the outer contour as an array of "
                               "vertices, got \"square\"");

    part = worktopJson();
    part["shape"]["outer"][1] = Json::array({1200, 0});
    EXPECT_THAT(partError(part), StartsWith("/assemblies/0/parts/0/shape/outer/1: expected a contour vertex"));

    part = worktopJson();
    part["shape"]["outer"][2].erase("y");
    EXPECT_THAT(partError(part), StartsWith("/assemblies/0/parts/0/shape/outer/2/y: "));

    part = worktopJson();
    part["shape"]["cutouts"][1][0]["bulge"] = "1";
    EXPECT_THAT(partError(part), StartsWith("/assemblies/0/parts/0/shape/cutouts/1/0/bulge: expected a number"));

    part = worktopJson();
    part["shape"]["outer"][0]["z"] = 0;
    EXPECT_THAT(partError(part), StartsWith("/assemblies/0/parts/0/shape/outer/0/z: unknown key"));

    part = worktopJson();
    part["shape"]["holes"] = Json::array();
    EXPECT_THAT(partError(part), StartsWith("/assemblies/0/parts/0/shape/holes: unknown key"));

    part = worktopJson();
    part["shape"]["cutouts"] = Json::object();
    EXPECT_THAT(partError(part), StartsWith("/assemblies/0/parts/0/shape/cutouts: expected an array"));

    part = worktopJson();
    part["shape"]["cutouts"][0] = 3;
    EXPECT_EQ(partError(part), "/assemblies/0/parts/0/shape/cutouts/0: expected cut-out 0 as an array of vertices, "
                               "got 3");

    part = worktopJson();
    part["shape"]["outerEdges"] = "eb.abs2";
    EXPECT_THAT(partError(part), StartsWith("/assemblies/0/parts/0/shape/outerEdges: expected an array"));

    part = worktopJson();
    part["shape"]["outerEdges"][1] = "";
    EXPECT_THAT(partError(part), StartsWith("/assemblies/0/parts/0/shape/outerEdges/1: "));

    part = worktopJson();
    part["shape"]["outerEdges"][2] = 7;
    EXPECT_THAT(partError(part), StartsWith("/assemblies/0/parts/0/shape/outerEdges/2: expected a string"));
}

TEST(ShapeCodecErrors, Limits) {
    // 10 001 vertices on the outer contour: refused before any vertex is read.
    Json part = worktopJson();
    part["shape"]["outer"] = Json::array();
    for (int i = 0; i < 10001; ++i) part["shape"]["outer"].push_back(Json::object());
    EXPECT_EQ(partError(part), "/assemblies/0/parts/0/shape/outer: the outer contour has 10001 vertices; at most "
                               "10000 are allowed per contour");

    part = worktopJson();
    part["shape"]["cutouts"][1] = Json::array();
    for (int i = 0; i < 10001; ++i) part["shape"]["cutouts"][1].push_back(Json::object());
    EXPECT_EQ(partError(part), "/assemblies/0/parts/0/shape/cutouts/1: cut-out 1 has 10001 vertices; at most "
                               "10000 are allowed per contour");

    part = worktopJson();
    part["shape"]["cutouts"] = Json::array();
    for (int i = 0; i < 1001; ++i) part["shape"]["cutouts"].push_back(Json::array());
    EXPECT_EQ(partError(part), "/assemblies/0/parts/0/shape/cutouts: the part has 1001 cut-outs; at most 1000 are "
                               "allowed");

    // Total over outer + cut-outs: 4 + 10000 + 10000 = 20004 > 20000, refused at the second cut-out.
    part = worktopJson();
    part["shape"]["cutouts"] = Json::array({Json::array(), Json::array()});
    for (int i = 0; i < 10000; ++i) {
        part["shape"]["cutouts"][0].push_back(Json::object());
        part["shape"]["cutouts"][1].push_back(Json::object());
    }
    EXPECT_EQ(partError(part), "/assemblies/0/parts/0/shape/cutouts/1: the shape has at least 20004 vertices in its "
                               "outer contour and cut-outs; at most 20000 are allowed per part");

    part = worktopJson();
    part["shape"]["outerEdges"] = Json::array();
    for (int i = 0; i < 10001; ++i) part["shape"]["outerEdges"].push_back(nullptr);
    EXPECT_THAT(partError(part), StartsWith("/assemblies/0/parts/0/shape/outerEdges: has 10001 entries; at most "
                                            "10000"));

    // Exactly at the limits is accepted: 1000 cut-outs (10 x 10 mm squares on a 20 mm grid,
    // 40 columns x 25 rows inside a 1200 x 600 panel) and a 10 000-vertex outer contour
    // (the bottom edge split into 9 997 collinear pieces, plus the two top corners).
    part = worktopJson();
    part["shape"]["cutouts"] = Json::array();
    for (int row = 0; row < 25; ++row)
        for (int col = 0; col < 40; ++col)
            part["shape"]["cutouts"].push_back(squareJson(105.0 + 20.0 * col, 45.0 + 20.0 * row, 10.0));
    auto ok = partFromJson(part, kPartPath);
    ASSERT_TRUE(ok.ok()) << ok.error().message;
    EXPECT_EQ(ok.value().shape->cutouts.size(), 1000u);

    part = worktopJson();
    part["shape"]["outer"] = Json::array();
    for (int i = 0; i < 9998; ++i) part["shape"]["outer"].push_back(vertex(1200.0 * i / 9997.0, 0)); // ends at (1200, 0)
    part["shape"]["outer"].push_back(vertex(1200, 600));
    part["shape"]["outer"].push_back(vertex(0, 600));
    part["shape"]["outerEdges"] = Json::array();
    ASSERT_EQ(part["shape"]["outer"].size(), 10000u);
    auto big = partFromJson(part, kPartPath);
    ASSERT_TRUE(big.ok()) << big.error().message;
}

// ---------------------------------------------------------------- geometric errors

TEST(ShapeCodecErrors, InvalidOuterContour) {
    // Bow tie: segments 0 and 2 cross at (600, 300).
    Json part = worktopJson();
    part["shape"]["outer"] = Json::array({vertex(0, 0), vertex(1200, 600), vertex(1200, 0), vertex(0, 600)});
    const std::string message = partError(part);
    EXPECT_THAT(message, StartsWith("/assemblies/0/parts/0/shape/outer: invalid outer contour: "));
    EXPECT_THAT(message, HasSubstr("(600, 300)"));

    part = worktopJson();
    part["shape"]["outer"][2]["bulge"] = 2e6; // |bulge| must be < 1e6
    EXPECT_THAT(partError(part), StartsWith("/assemblies/0/parts/0/shape/outer: invalid outer contour: "));

    part = worktopJson();
    part["shape"]["outer"] = Json::array({vertex(0, 0), vertex(1200, 0)}); // two straight segments
    EXPECT_THAT(partError(part), StartsWith("/assemblies/0/parts/0/shape/outer: invalid outer contour: "));
}

TEST(ShapeCodecErrors, ClockwiseOuterContour) {
    Json part = worktopJson();
    part["shape"]["outer"] = Json::array({vertex(0, 0), vertex(0, 600), vertex(1200, 600), vertex(1200, 0)});
    // Signed area -1200 * 600 = -720000.
    EXPECT_EQ(partError(part), "/assemblies/0/parts/0/shape/outer: the outer contour is clockwise (signed area "
                               "-720000 mm^2); it must be counter-clockwise");
}

TEST(ShapeCodecErrors, BoundingBoxMustMatchLengthAndWidth) {
    Json part = toJson(formats::test::arcTopSide());
    part["width"] = 600; // the arc rises to y = 700
    EXPECT_EQ(partError(part), "/assemblies/0/parts/0/shape/outer: the bounding box of the outer contour is "
                               "[0, 800] x [0, 700] mm but the part's length x width 800 x 600 mm requires "
                               "[0, 800] x [0, 600] (tolerance 0.000001 mm)");
    // Shifted outline: right size, wrong place.
    Json shifted = worktopJson();
    for (auto& v : shifted["shape"]["outer"]) v["x"] = v["x"].get<double>() + 10.0;
    EXPECT_THAT(partError(shifted), HasSubstr("is [10, 1210] x [0, 600] mm"));
}

TEST(ShapeCodecErrors, InvalidCutout) {
    Json part = worktopJson();
    part["shape"]["cutouts"][0] = Json::array({vertex(400, 200), vertex(500, 200)});
    EXPECT_THAT(partError(part), StartsWith("/assemblies/0/parts/0/shape/cutouts/0: invalid cut-out 0: "));
}

TEST(ShapeCodecErrors, CutoutVertexOutsideOrOnTheOuterContour) {
    Json part = worktopJson();
    part["shape"]["cutouts"][0][2] = vertex(850, 650); // above the top edge y = 600
    EXPECT_EQ(partError(part), "/assemblies/0/parts/0/shape/cutouts/0/2: vertex 2 (850, 650) of cut-out 0 lies "
                               "outside the outer contour; cut-outs must lie strictly inside the outer contour");
    part = worktopJson();
    part["shape"]["cutouts"][0][2] = vertex(850, 600); // exactly on the top edge
    EXPECT_EQ(partError(part), "/assemblies/0/parts/0/shape/cutouts/0/2: vertex 2 (850, 600) of cut-out 0 lies on "
                               "the outer contour (closer than 0.000001 mm); cut-outs must lie strictly inside the "
                               "outer contour");
}

TEST(ShapeCodecErrors, CutoutArcCrossingTheOuterContour) {
    // Square 1100..1180 x 250..350 whose right side bulges out as a half circle (bulge 1):
    // radius 50 around (1180, 300) reaches x = 1230 > 1200, although all vertices are inside.
    Json part = worktopJson();
    part["shape"]["cutouts"][1] =
        Json::array({vertex(1100, 250), vertex(1180, 250, 1.0), vertex(1180, 350), vertex(1100, 350)});
    const std::string message = partError(part);
    EXPECT_THAT(message, StartsWith("/assemblies/0/parts/0/shape/cutouts/1: segment 1 of cut-out 1 touches or "
                                    "crosses segment 1 of the outer contour at (1200, "));
    EXPECT_THAT(message, HasSubstr("cut-outs must lie strictly inside the outer contour"));
}

TEST(ShapeCodecErrors, CutoutsTouchingOrNested) {
    Json part = worktopJson();
    // Hob circle moved onto the sink's right side x = 850: circle (870, 300) r 30 crosses it.
    part["shape"]["cutouts"][1] = Json::array({vertex(900, 300, 1.0), vertex(840, 300, 1.0)});
    EXPECT_THAT(partError(part), StartsWith("/assemblies/0/parts/0/shape/cutouts/1: segment "));
    EXPECT_THAT(partError(part), HasSubstr("of cut-out 1 touches or crosses segment 1 of cut-out 0"));
    EXPECT_THAT(partError(part), HasSubstr("cut-outs must not touch or overlap each other"));

    part = worktopJson();
    part["shape"]["cutouts"][1] = Json::array({vertex(630, 300, 1.0), vertex(570, 300, 1.0)}); // inside the sink
    EXPECT_EQ(partError(part), "/assemblies/0/parts/0/shape/cutouts/1: cut-out 1 lies inside cut-out 0 (its vertex 0 "
                               "(630, 300) is enclosed); nested cut-outs are not allowed");

    part = worktopJson();
    part["shape"]["cutouts"] = Json::array({squareJson(500, 200, 50), squareJson(400, 100, 300)});
    EXPECT_EQ(partError(part), "/assemblies/0/parts/0/shape/cutouts/1: cut-out 1 encloses cut-out 0 (vertex 0 "
                               "(500, 200) of cut-out 0 is inside it); nested cut-outs are not allowed");
}

TEST(ShapeCodecErrors, OuterEdgesCountAndPartEdges) {
    Json part = worktopJson();
    part["shape"]["outerEdges"] = Json::array({"eb.abs2", nullptr});
    EXPECT_EQ(partError(part), "/assemblies/0/parts/0/shape/outerEdges: has 2 entries but the outer contour has 4 "
                               "segments; give one entry (edge band id or null) per segment, or an empty array when "
                               "no segment is banded");

    part = worktopJson();
    part["edges"] = Json::parse(R"({"l1": null, "w2": "eb.abs04"})");
    EXPECT_EQ(partError(part), "/assemblies/0/parts/0/edges/w2: the part has a shape, so its edge bands are given per "
                               "outer segment in \"shape\".\"outerEdges\" and \"edges\" must be empty, but edge w2 has "
                               "the band \"eb.abs04\"");
    // Explicit nulls ("raw") in "edges" are fine.
    part["edges"] = Json::parse(R"({"l1": null})");
    EXPECT_TRUE(partFromJson(part, kPartPath).ok());
}

TEST(ShapeCodecErrors, ErrorsInsideAModelCarryTheFullPath) {
    Json model = toJson(formats::test::shapedModel());
    model["assemblies"][1]["parts"][2]["shape"]["cutouts"][0][1]["x"] = 1300;
    auto decoded = modelFromJson(model);
    ASSERT_FALSE(decoded.ok());
    EXPECT_EQ(decoded.error().context, "/assemblies/1/parts/2/shape/cutouts/0/1");
}

TEST(ShapeCodecErrors, CheckedEncodingRefusesAnInvalidInMemoryShape) {
    domain::Model model = formats::test::shapedModel();
    model.assemblies[1].parts[1].width = 650; // arc-top outline is 700 high
    auto checked = modelToJsonChecked(model);
    ASSERT_FALSE(checked.ok());
    EXPECT_THAT(checked.error().message, HasSubstr("would not load again: /assemblies/1/parts/1/shape/outer: the "
                                                   "bounding box"));
    model = formats::test::shapedModel();
    model.assemblies[1].parts[0].edge(domain::EdgeSide::L1) = domain::EdgeBandId("eb.abs2");
    EXPECT_FALSE(modelToJsonChecked(model).ok());
    model = formats::test::shapedModel();
    model.assemblies[1].parts[0].shape->outerEdges[3] = domain::EdgeBandId("");
    checked = modelToJsonChecked(model);
    ASSERT_FALSE(checked.ok());
    EXPECT_THAT(checked.error().message, HasSubstr("/assemblies/1/parts/0/shape/outerEdges/3"));
}
