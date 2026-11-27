// SPDX-License-Identifier: AGPL-3.0-only
// Pictorial (isometric / exploded) drawing requests of the Drawings workspace and their export
// through the existing SVG / PDF paths.
#include "fos/app/drawing_edits.hpp"
#include "fos/app/drawing_request.hpp"
#include "test_support.hpp"

#include <fstream>
#include <sstream>

namespace fos::app::test {
namespace {

std::size_t balloonPrimitives(const drawings::Sheet& sheet) {
    std::size_t n = 0;
    for (const auto& p : sheet.primitives) n += drawings::elementIdOf(p).find("/balloon[") != std::string::npos;
    return n;
}

struct Pictorial : ::testing::Test {
    Session s;
    void SetUp() override { ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok()); }
    DrawingRequest request(DrawingRequest::Kind kind) const {
        DrawingRequest r;
        r.kind = kind;
        r.id = "cab1";
        r.date = "2026-11-27";
        return r;
    }
};

TEST_F(Pictorial, IsometricAndExplodedSheets) {
    auto iso = generateDrawing(s.project(), *s.resolved(), request(DrawingRequest::Kind::Isometric));
    ASSERT_TRUE(iso.ok()) << iso.error().message;
    ASSERT_EQ(iso.value().sheets.size(), 1u);
    EXPECT_TRUE(iso.value().views.empty()); // no orthographic views -> no DXF / annotation edits
    EXPECT_GT(balloonPrimitives(iso.value().sheets[0]), 0u);

    auto noBalloons = request(DrawingRequest::Kind::Isometric);
    noBalloons.balloons = false;
    auto plain = generateDrawing(s.project(), *s.resolved(), noBalloons);
    ASSERT_TRUE(plain.ok());
    EXPECT_EQ(balloonPrimitives(plain.value().sheets[0]), 0u);

    auto exploded = request(DrawingRequest::Kind::Exploded);
    exploded.direction = drawings::IsoDirection::FrontRight;
    exploded.explodeFactor = 2.0;
    auto ex = generateDrawing(s.project(), *s.resolved(), exploded);
    ASSERT_TRUE(ex.ok()) << ex.error().message;
    ASSERT_EQ(ex.value().sheets.size(), 1u);

    // The explode factor is judged by the engine ([1, 10]); unknown items are NotFound.
    exploded.explodeFactor = 0.5;
    EXPECT_FALSE(generateDrawing(s.project(), *s.resolved(), exploded).ok());
    auto missing = request(DrawingRequest::Kind::Isometric);
    missing.id = "nope";
    auto nf = generateDrawing(s.project(), *s.resolved(), missing);
    ASSERT_FALSE(nf.ok());
    EXPECT_EQ(nf.error().code, core::ErrorCode::NotFound);

    EXPECT_TRUE(isPictorial(exploded));
    EXPECT_FALSE(isPictorial(request(DrawingRequest::Kind::Assembly)));
    EXPECT_TRUE(drawingIdOf(exploded).empty());
}

TEST_F(Pictorial, ExportSvgPdfButNoDxf) {
    auto iso = generateDrawing(s.project(), *s.resolved(), request(DrawingRequest::Kind::Isometric));
    ASSERT_TRUE(iso.ok());
    TempDir dir;
    auto svg = exportDrawingSvg(iso.value(), dir.path / "iso.svg");
    ASSERT_TRUE(svg.ok()) << svg.error().message;
    ASSERT_EQ(svg.value().size(), 1u);
    std::ifstream in(svg.value()[0]);
    std::stringstream ss;
    ss << in.rdbuf();
    EXPECT_NE(ss.str().find("<svg"), std::string::npos);
    ASSERT_TRUE(exportDrawingPdf(iso.value(), dir.path / "iso.pdf", "iso").ok());
    EXPECT_GT(std::filesystem::file_size(dir.path / "iso.pdf"), 0u);
    EXPECT_FALSE(exportDrawingDxf(iso.value(), dir.path / "iso.dxf").ok());
}

} // namespace
} // namespace fos::app::test
