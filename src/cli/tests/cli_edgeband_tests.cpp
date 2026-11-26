// SPDX-License-Identifier: AGPL-3.0-only
// `fos edgeband-jobs`. Reference project: newBaseCabinet (W 800), whose shelf 762 x 540 has its
// front edge L1 banded with ABS 2 mm; the project's overhang is 25 mm per strip end, so the
// shelf front strip is 762 + 2 * 25 = 812 mm and its cut size 762 x (540 - 2) = 538.
#include "cli_test_support.hpp"

#include "fos/project/document.hpp"
#include "fos/project/file.hpp"

namespace fos::cli::test {
namespace {

TEST(CliEdgebandJobs, TextCsvJsonAndSvg) {
    TempDir dir;
    const auto project = dir.file("kitchen.fosp");
    ASSERT_EQ(newBaseCabinet(project).code, kExitOk);

    auto r = fos({"edgeband-jobs", project});
    ASSERT_EQ(r.code, kExitOk) << r.out << r.err;
    EXPECT_TRUE(contains(r.out, "Edge banding jobs (overhang 25 mm per strip end)")) << r.out;
    EXPECT_TRUE(contains(r.out, "long edges first, then short edges")) << r.out;
    EXPECT_TRUE(contains(r.out, "(cab1/shelf.1)  cut 762 x 538  finished 762 x 540")) << r.out;
    EXPECT_TRUE(contains(r.out, "L1  cut edge 762  edge 762  strip 812")) << r.out;

    r = fos({"edgeband-jobs", project, "--format", "csv"});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    EXPECT_TRUE(contains(r.out, "Band,Band name,Band thickness,Band width,Kind,No.,Part ID,Part,Cut length,"
                                "Cut width,Edge,Step,Cut edge length,Edge length,Strip length"))
        << r.out;
    EXPECT_TRUE(contains(r.out, ",cab1/shelf.1,")) << r.out;
    EXPECT_TRUE(contains(r.out, ",762,538,L1,")) << r.out;
    EXPECT_TRUE(contains(r.out, ",762,762,812\r\n")) << r.out; // toCsv: CRLF rows
    EXPECT_TRUE(contains(r.out, ",L1,1/1,")) << r.out;

    r = fos({"edgeband-jobs", project, "--format", "json", "--svg", dir.file("svg")});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    EXPECT_TRUE(contains(r.out, "\"partId\": \"cab1/shelf.1\"")) << r.out;
    EXPECT_TRUE(contains(r.out, "\"edgeLength\": 762, \"stripLength\": 812")) << r.out;
    EXPECT_TRUE(contains(r.err, "wrote ")) << r.err;
    bool found = false;
    for (const auto& e : std::filesystem::directory_iterator(u8path(dir.file("svg")))) {
        const std::string svg = readFile(e.path());
        EXPECT_TRUE(contains(svg, "viewBox=\"0 0 210 297\""));
        found = found || contains(svg, "edge 762, strip 812 (cut edge 762)");
    }
    EXPECT_TRUE(found);
}

TEST(CliEdgebandJobs, ShapedShelfGoesToContourBanding) {
    TempDir dir;
    const auto project = dir.file("shaped.fosp");
    ASSERT_EQ(newBaseCabinet(project).code, kExitOk);
    auto loaded = project::loadProject(u8path(project));
    ASSERT_TRUE(loaded.ok());
    project::Document doc(loaded.value().project);
    furniture::ContourSpec front;
    front.kind = furniture::ContourKind::RoundedCorners;
    front.radii = {"30", "30", "", ""};
    ASSERT_TRUE(doc.execute(project::commands::setPartContour("cab1", "shelf.1", front)).ok());
    ASSERT_TRUE(project::saveProject(u8path(project), doc.project()).ok());

    const auto r = fos({"edgeband-jobs", project});
    ASSERT_EQ(r.code, kExitOk) << r.out << r.err;
    EXPECT_TRUE(contains(r.out, "Shaped parts: contour banding - manual / edge bander with contour unit")) << r.out;
    EXPECT_TRUE(contains(r.out, "(cab1/shelf.1)  blank ")) << r.out;
    EXPECT_FALSE(contains(r.out, "(cab1/shelf.1)  cut ")) << r.out; // not on the straight list
}

TEST(CliEdgebandJobs, UsageErrors) {
    TempDir dir;
    const auto project = dir.file("kitchen.fosp");
    ASSERT_EQ(newBaseCabinet(project).code, kExitOk);
    EXPECT_EQ(fos({"edgeband-jobs"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"edgeband-jobs", project, "--format", "xml"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"edgeband-jobs", dir.file("missing.fosp")}).code, kExitUsageOrIo);
    const auto h = fos({"edgeband-jobs", "--help"});
    EXPECT_EQ(h.code, kExitOk);
    EXPECT_TRUE(contains(h.out, "Usage: fos edgeband-jobs"));
    // Unknown assembly: empty list with a warning, still exit 0.
    const auto u = fos({"edgeband-jobs", project, "--assembly", "nope"});
    EXPECT_EQ(u.code, kExitOk);
    EXPECT_TRUE(contains(u.out, "production.scope.unknown_assembly")) << u.out;
}

} // namespace
} // namespace fos::cli::test
