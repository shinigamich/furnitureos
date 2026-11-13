// SPDX-License-Identifier: AGPL-3.0-only
// In-process tests of every `fos` command: exit codes, messages and written files.
//
// Reference project (newBaseCabinet): base-cabinet template, W 800 x H 720 x D 560, generic
// 18 mm particleboard, plinth 100 (sides to the floor), 18 mm boards, so by hand:
//   inner width = 800 - 2 * 18 = 764 (bottom, rails, plinth); shelf = 764 - 2 * 1 = 762 x (560 - 20) = 540
//   sides 720 x 560; back (800 - 2*2) x (720 - 100 - 2*2) = 796 x 616
//   doors: height 620 - 2 * 2 = 616, width (800 - 2*2 - 3) / 2 = 396.5
//   cut sizes = finished - edge bands: bottom 764 x (560 - 2) = 558; side (720 - 0.4 - 0.4) x 558 = 719.2 x 558
#include "cli_test_support.hpp"

#include "fos/project/file.hpp"

namespace fos::cli::test {
namespace {

class Cli : public ::testing::Test {
protected:
    TempDir dir;
    std::string project = dir.file("kitchen.fosp");
    void SetUp() override {
        const auto r = newBaseCabinet(project);
        ASSERT_EQ(r.code, kExitOk) << r.err;
    }
};

TEST(CliGeneral, NoArgumentsPrintsOverviewAndFailsWithUsage) {
    const auto r = fos({});
    EXPECT_EQ(r.code, kExitUsageOrIo);
    EXPECT_TRUE(contains(r.err, "Usage: fos <command>"));
}

TEST(CliGeneral, HelpListsEveryCommand) {
    const auto r = fos({"--help"});
    EXPECT_EQ(r.code, kExitOk);
    for (const char* c : {"new", "info", "set", "validate", "report", "cut", "drawing", "labels", "cam", "export-obj"})
        EXPECT_TRUE(contains(r.out, std::string("  ") + c + " ")) << c;
    EXPECT_FALSE(contains(r.out, "nest")); // not offered: projects have no polygon parts yet
}

TEST(CliGeneral, EveryCommandHasHelp) {
    for (const char* c : {"new", "info", "set", "validate", "report", "cut", "drawing", "labels", "cam", "export-obj"}) {
        const auto r = fos({c, "--help"});
        EXPECT_EQ(r.code, kExitOk) << c;
        EXPECT_EQ(r.out.rfind(std::string("Usage: fos ") + c, 0), 0u) << c << ": " << r.out;
    }
}

TEST(CliGeneral, UnknownCommandAndOptionAreUsageErrors) {
    EXPECT_EQ(fos({"nest", "x.fosp"}).code, kExitUsageOrIo);
    const auto r = fos({"info", "x.fosp", "--bogus"});
    EXPECT_EQ(r.code, kExitUsageOrIo);
    EXPECT_TRUE(contains(r.err, "unknown option '--bogus'"));
    EXPECT_TRUE(contains(fos({"report", "x.fosp", "--type"}).err, "needs a value"));
}

TEST(CliGeneral, MissingAndCorruptFilesAreIoErrors) {
    TempDir d;
    const auto missing = fos({"info", d.file("none.fosp")});
    EXPECT_EQ(missing.code, kExitUsageOrIo);
    EXPECT_TRUE(contains(missing.err, "cannot load"));
    std::ofstream(d.file("bad.fosp")) << "{ not json";
    for (const char* c : {"info", "validate", "cut"}) EXPECT_EQ(fos({c, d.file("bad.fosp")}).code, kExitUsageOrIo) << c;
}

TEST_F(Cli, NewWritesALoadableProject) {
    auto loaded = project::loadProject(project);
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    const auto& p = loaded.value().project;
    EXPECT_EQ(p.metadata.id, kTestUuid);
    EXPECT_EQ(p.metadata.name, "Kitchen");
    EXPECT_EQ(p.metadata.createdAt, kTestDate);
    ASSERT_EQ(p.items.size(), 1u);
    EXPECT_EQ(p.items[0].id(), "cab1");
    EXPECT_DOUBLE_EQ(p.items[0].cabinet()->parameters.value("width").value(), 800.0);
    EXPECT_DOUBLE_EQ(p.items[0].cabinet()->parameters.value("height").value(), 720.0);
    EXPECT_DOUBLE_EQ(p.items[0].cabinet()->parameters.value("depth").value(), 560.0); // template default
}

TEST_F(Cli, NewRefusesToOverwriteWithoutForceAndChecksArguments) {
    const std::string before = readFile(project);
    const auto again = newBaseCabinet(project);
    EXPECT_EQ(again.code, kExitUsageOrIo);
    EXPECT_TRUE(contains(again.err, "already exists"));
    EXPECT_EQ(readFile(project), before);
    EXPECT_EQ(fos({"new", project, "--force", "--id", kTestUuid, "--date", kTestDate}).code, kExitOk);
    EXPECT_EQ(fos({"new", dir.file("a.fosp"), "--template", "sofa"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"new", dir.file("b.fosp"), "--width", "900"}).code, kExitUsageOrIo); // needs --template
    EXPECT_EQ(fos({"new", dir.file("c.fosp"), "--template", "base", "--width", "wide"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"new", dir.file("d.fosp"), "--date", "yesterday"}).code, kExitUsageOrIo);
    // 20 mm wide: 20 - 2 * 18 = -4 mm inner width -> refused by the model rules, nothing written.
    EXPECT_EQ(fos({"new", dir.file("e.fosp"), "--template", "base", "--width", "20"}).code, kExitValidationErrors);
    EXPECT_FALSE(std::filesystem::exists(dir.file("e.fosp")));
}

TEST_F(Cli, NewAcceptsEveryTemplate) {
    for (const char* t : {"base", "wall", "tall", "drawers", "wardrobe", "shelving"}) {
        const auto r = fos({"new", dir.file(std::string(t) + ".fosp"), "--template", t});
        EXPECT_EQ(r.code, kExitOk) << t << r.err;
        EXPECT_EQ(fos({"validate", dir.file(std::string(t) + ".fosp")}).code, kExitOk) << t;
    }
}

TEST_F(Cli, InfoSummarisesItemsParametersAndParts) {
    const auto r = fos({"info", project});
    EXPECT_EQ(r.code, kExitOk) << r.err;
    EXPECT_TRUE(contains(r.out, "Project: Kitchen"));
    EXPECT_TRUE(contains(r.out, "cab1  cabinet  \"Base cabinet\"  W 800 x H 720 x D 560 mm  parts: 10"));
    EXPECT_TRUE(contains(r.out, "      width = 800\n"));
    EXPECT_FALSE(contains(r.out, "inner_width")); // formulas only with --all
    EXPECT_TRUE(contains(r.out, "Diagnostics: none"));
    const auto all = fos({"info", project, "--all"});
    EXPECT_TRUE(contains(all.out, "inner_width = width - 2 * thickness  -> 764")); // 800 - 36
}

TEST_F(Cli, SetChangesTheParameterAndSavesAtomically) {
    const auto r = fos({"set", project, "cab1", "width", "900"});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    EXPECT_TRUE(contains(r.out, "width of cab1: 800 -> 900"));
    EXPECT_TRUE(std::filesystem::exists(project + ".bak1")); // previous version kept
    auto loaded = project::loadProject(project);
    ASSERT_TRUE(loaded.ok());
    const auto& model = loaded.value().resolved.model;
    EXPECT_DOUBLE_EQ(model.assemblies[0].findPart(domain::PartId("cab1/bottom"))->length, 864.0); // 900 - 36

    // Expression: depth = height / 2 = 720 / 2 = 360.
    const auto e = fos({"set", project, "cab1", "depth", "height / 2"});
    ASSERT_EQ(e.code, kExitOk) << e.err;
    EXPECT_TRUE(contains(e.out, "depth of cab1: 560 -> 360"));
}

TEST_F(Cli, SetRefusalsLeaveTheFileUnchanged) {
    const std::string before = readFile(project);
    const auto refused = fos({"set", project, "cab1", "width", "20"});
    EXPECT_EQ(refused.code, kExitValidationErrors);
    EXPECT_TRUE(contains(refused.err, "Inner width must be positive"));
    EXPECT_EQ(fos({"set", project, "cab1", "width", "widht + 1"}).code, kExitValidationErrors);
    EXPECT_EQ(fos({"set", project, "cab9", "width", "900"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"set", project, "cab1", "no_such", "1"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"set", project, "cab1", "width"}).code, kExitUsageOrIo);
    EXPECT_EQ(readFile(project), before);
}

TEST_F(Cli, SetThatBreaksTheModelIsSavedButReportedAsErrors) {
    // bottom_length pinned to 800 > 764 inner width: the bottom runs 18 + 800 = 818 > 782 into
    // the right side -> generator error furniture.geometry.interpenetration.
    const auto r = fos({"set", project, "cab1", "bottom_length", "800"});
    EXPECT_EQ(r.code, kExitValidationErrors);
    EXPECT_TRUE(contains(r.out, "furniture.geometry.interpenetration"));
    EXPECT_TRUE(contains(r.err, "bottom_length \"inner_width\"' restores"));
    const auto v = fos({"validate", project});
    EXPECT_EQ(v.code, kExitValidationErrors);
    EXPECT_TRUE(contains(v.out, "Validation FAILED"));
    EXPECT_TRUE(contains(v.out, "[error] validation.interference.parts (cab1/bottom)"));
    EXPECT_EQ(fos({"info", project}).code, kExitValidationErrors);
}

TEST_F(Cli, ValidateTextAndMarkdown) {
    const auto text = fos({"validate", project});
    EXPECT_EQ(text.code, kExitOk) << text.out;
    EXPECT_TRUE(contains(text.out, "Generator diagnostics: none"));
    EXPECT_TRUE(contains(text.out, "Validation passed: 0 error(s)"));
    const auto md = fos({"validate", project, "--format", "markdown"});
    EXPECT_EQ(md.code, kExitOk);
    EXPECT_EQ(md.out.rfind("# Validation of Kitchen", 0), 0u);
    EXPECT_EQ(fos({"validate", project, "--format", "html"}).code, kExitUsageOrIo);
}

TEST_F(Cli, ReportTablesForEveryType) {
    for (const char* t : {"parts", "cutlist", "materials", "edgebanding", "hardware", "machining", "summary"}) {
        const auto r = fos({"report", project, "--type", t});
        EXPECT_EQ(r.code, kExitOk) << t << r.err;
        EXPECT_FALSE(r.out.empty()) << t;
    }
    const auto parts = fos({"report", project, "--type", "parts"});
    // Row of the bottom: finished 764 x 560, cut 764 x 558 (2 mm ABS on the front edge L1).
    EXPECT_TRUE(contains(parts.out, "cab1/bottom      Bottom      Bottom  Particleboard 18 mm (generic)  764     560    18         764         558"));
    const auto summary = fos({"report", project, "--type", "summary"});
    EXPECT_TRUE(contains(summary.out, "Total: 10 parts"));
    EXPECT_TRUE(contains(fos({"report", project, "--type", "materials"}).out, "area lower bounds"));
    EXPECT_EQ(fos({"report", project, "--type", "prices"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"report", project}).code, kExitUsageOrIo);
}

TEST_F(Cli, ReportCsvWithSeparator) {
    const std::string csv = dir.file("cut.csv");
    const auto r = fos({"report", project, "--type", "cutlist", "--csv", csv, "--separator", ";"});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    const std::string text = readFile(csv);
    EXPECT_EQ(text.rfind("Material;Thickness;Cut length;Cut width;Qty;", 0), 0u);
    // Rear rail and plinth: both 764 x 100 raw -> one row with quantity 2.
    EXPECT_TRUE(contains(text, "\r\nParticleboard 18 mm (generic);18;764;100;2;764;100;"));
    EXPECT_EQ(fos({"report", project, "--type", "cutlist", "--separator", ";"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"report", project, "--type", "cutlist", "--csv", csv, "--separator", "ab"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"report", project, "--type", "cutlist", "--csv", dir.file("no/dir/x.csv")}).code, kExitUsageOrIo);
}

TEST_F(Cli, ReportHtmlProductionSheet) {
    const std::string html = dir.file("sheet.html");
    ASSERT_EQ(fos({"report", project, "--html", html, "cab1"}).code, kExitOk);
    const std::string text = readFile(html);
    EXPECT_TRUE(contains(text, "<html"));
    EXPECT_TRUE(contains(text, "cab1/side.left"));
    EXPECT_EQ(fos({"report", project, "--html", html}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"report", project, "--html", html, "cab7"}).code, kExitUsageOrIo);
}

TEST_F(Cli, CutWritesSheetsAndHonestSummary) {
    const std::string out = dir.file("cut");
    const auto r = fos({"cut", project, "--out-dir", out});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    EXPECT_TRUE(contains(r.out, "HEURISTIC"));
    EXPECT_TRUE(contains(r.out, "(heuristic result)"));
    EXPECT_TRUE(contains(r.out, "pieces: 9 required, 9 placed, 0 unplaced")); // 10 parts minus the HDF back
    EXPECT_TRUE(contains(r.out, "validator (cutting::validateLayout): no findings"));
    EXPECT_TRUE(std::filesystem::exists(out + "/mat.generic.pb18-sheet-1.svg"));
    EXPECT_TRUE(std::filesystem::exists(out + "/mat.generic.hdf3-sheet-1.svg"));
    EXPECT_EQ(readFile(out + "/cutting-summary.txt") + "Wrote 2 sheet SVG(s) and cutting-summary.txt to " + out + "\n",
              r.out);
    // HDF 3 back 796 x 616 on a 2800 x 2070 sheet: 490336 / 5796000 = 8.46 %.
    EXPECT_TRUE(contains(r.out, "utilisation 8.46 % of the sheet"));
}

TEST_F(Cli, DrawingSvgDxfAndScaleErrors) {
    const std::string svg = dir.file("d.svg"), dxf = dir.file("d.dxf");
    const auto r = fos({"drawing", project, "cab1", "--svg", svg, "--dxf", dxf});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    EXPECT_TRUE(contains(r.out, "A3 landscape 420 x 297 mm, scale 1:10, 3 view(s)"));
    const std::string s = readFile(svg);
    EXPECT_TRUE(contains(s, "width=\"420mm\" height=\"297mm\" viewBox=\"0 0 420 297\""));
    EXPECT_TRUE(contains(s, "data-element=\"cab1/view.front/dim.overall.width\">800<"));
    const std::string d = readFile(dxf);
    EXPECT_TRUE(contains(d, "AC1009"));
    EXPECT_EQ(d.substr(d.size() - 4), "EOF\n");
    // Part drawing on A4.
    ASSERT_EQ(fos({"drawing", project, "cab1/side.left", "--svg", dir.file("p.svg"), "--paper", "A4"}).code, kExitOk);
    EXPECT_TRUE(contains(readFile(dir.file("p.svg")), "width=\"297mm\" height=\"210mm\""));
    // 1:1 on A4 cannot fit an 800 mm cabinet: explicit scales are never changed silently.
    const auto big = fos({"drawing", project, "cab1", "--svg", dir.file("x.svg"), "--paper", "A4", "--scale", "1:1"});
    EXPECT_EQ(big.code, kExitValidationErrors);
    EXPECT_TRUE(contains(big.err, "At scale 1:1"));
    EXPECT_EQ(fos({"drawing", project, "cab1", "--svg", svg, "--paper", "B5"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"drawing", project, "cab1", "--svg", svg, "--scale", "ten"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"drawing", project, "cab1"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"drawing", project, "nothing", "--svg", svg}).code, kExitUsageOrIo);
}

TEST_F(Cli, LabelsOnePagePerTwentyFourParts) {
    const auto r = fos({"labels", project, "--svg-dir", dir.file("labels")});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    // 10 parts, 3 x 8 = 24 labels per A4 page -> 1 page.
    EXPECT_TRUE(contains(r.out, "Wrote 10 labels on 1 page(s)"));
    EXPECT_TRUE(contains(readFile(dir.file("labels/labels-1.svg")), "width=\"210mm\" height=\"297mm\""));
    EXPECT_FALSE(std::filesystem::exists(dir.file("labels/labels-2.svg")));
    EXPECT_EQ(fos({"labels", project}).code, kExitUsageOrIo);
}

TEST_F(Cli, CamWritesUnverifiedProgramForTheDemoProfile) {
    const std::string nc = dir.file("side_a.nc");
    const auto r = fos({"cam", project, "cab1/side.left", "--face", "A", "--out", nc});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    EXPECT_TRUE(contains(r.out, "verifiedForMachine: false"));
    EXPECT_TRUE(contains(r.out, "Simulation: 0 errors"));
    const std::string g = readFile(nc);
    EXPECT_TRUE(contains(g, "NOT VERIFIED AGAINST ANY CONTROLLER"));
    EXPECT_TRUE(contains(g, "demo profile — not a real machine"));
    EXPECT_TRUE(contains(g, "PROFILE NOT VERIFIED"));
    EXPECT_EQ(g.substr(g.size() - 4), "M30\n");
}

TEST_F(Cli, CamReportsEdgeHolesAsUnsupportedAndWritesNothing) {
    // The bottom only has confirmat holes in its end edges W1 / W2 (3 each).
    const std::string nc = dir.file("bottom.nc");
    const auto r = fos({"cam", project, "cab1/bottom", "--face", "A", "--out", nc});
    EXPECT_EQ(r.code, kExitValidationErrors);
    EXPECT_TRUE(contains(r.out, "UNSUPPORTED (not in this program): 6"));
    EXPECT_TRUE(contains(r.out, "is on an edge face"));
    EXPECT_FALSE(std::filesystem::exists(nc));
}

TEST_F(Cli, CamRefusesAProgramOutsideTheEnvelope) {
    // A 3300 mm tall side at machine X 20 .. 3320 exceeds the demo envelope X 0 .. 3200:
    // generateProgram refuses it (envelope check) and nothing is written. (The postprocessor's own
    // refusal of simulation errors is tested in src/cam/tests and tests/scenario_e.)
    const std::string tall = dir.file("tall.fosp");
    ASSERT_EQ(fos({"new", tall, "--template", "tall", "--height", "3300"}).code, kExitOk);
    const std::string nc = dir.file("tall.nc");
    const auto r = fos({"cam", tall, "cab1/side.left", "--face", "A", "--out", nc});
    EXPECT_EQ(r.code, kExitValidationErrors);
    EXPECT_TRUE(contains(r.err, "fos cam: refused: "));
    EXPECT_TRUE(contains(r.err, "outside the envelope of machine 'FurnitureOS demo profile"));
    EXPECT_TRUE(contains(r.err, "No program was written."));
    EXPECT_FALSE(std::filesystem::exists(nc));
}

TEST_F(Cli, CamArgumentErrors) {
    EXPECT_EQ(fos({"cam", project, "cab1/side.left", "--face", "L1", "--out", dir.file("x.nc")}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"cam", project, "cab1/side.left", "--face", "A"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"cam", project, "cab1/nothing", "--face", "A", "--out", dir.file("x.nc")}).code, kExitUsageOrIo);
}

TEST_F(Cli, ExportObjWritesObjAndMtl) {
    const std::string obj = dir.file("model.obj");
    const auto r = fos({"export-obj", project, obj});
    ASSERT_EQ(r.code, kExitOk) << r.err;
    const std::string text = readFile(obj);
    std::size_t objects = 0;
    for (std::size_t p = text.find("\no "); p != std::string::npos; p = text.find("\no ", p + 1)) ++objects;
    if (text.rfind("o ", 0) == 0) ++objects;
    EXPECT_EQ(objects, 10u); // one object per part
    EXPECT_TRUE(std::filesystem::exists(dir.file("model.mtl")));
    EXPECT_EQ(fos({"export-obj", project}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"export-obj", project, dir.file("no/dir/m.obj")}).code, kExitUsageOrIo);
}

// Project with the good reference cabinet cab1 and a copy cab2 whose pinned bottom_length is
// -100 mm: the generator refuses cab2 (furniture.geometry.invalid_size, reported as
// project.item.generation_failed) and its parts are missing from every output. Each output command must still write cab1's output, say
// which item is missing, list the error and exit 1.
TEST_F(Cli, OutputCommandsReportItemsThatFailedToGenerate) {
    auto loaded = project::loadProject(project);
    ASSERT_TRUE(loaded.ok());
    project::Project p = loaded.value().project;
    project::Item broken = p.items[0];
    broken.cabinet()->id = domain::AssemblyId("cab2");
    // The parameter graph accepts the value (no constraint on it); the generator refuses it.
    const auto pinned = broken.cabinet()->parameters.setValue("bottom_length", -100);
    ASSERT_TRUE(pinned.ok()) << pinned.error().message;
    broken.cabinet()->pinnedParameters.insert("bottom_length");
    p.items.push_back(broken);
    const std::string file = dir.file("broken.fosp");
    ASSERT_TRUE(project::saveProject(file, p).ok());
    const auto check = project::loadProject(file);
    ASSERT_TRUE(check.ok());
    ASSERT_EQ(check.value().resolved.items.size(), 2u);
    ASSERT_TRUE(check.value().resolved.items[0].generated);
    ASSERT_FALSE(check.value().resolved.items[1].generated);

    auto expectIncomplete = [&](const RunResult& r, const std::string& command) {
        EXPECT_EQ(r.code, kExitValidationErrors) << command << "\n" << r.out << r.err;
        EXPECT_TRUE(contains(r.err, "fos " + command + ": INCOMPLETE OUTPUT: the project has 1 model error(s)."))
            << command << "\n" << r.err;
        EXPECT_TRUE(contains(r.err, "MISSING from this output: cab2.")) << command << "\n" << r.err;
        EXPECT_TRUE(contains(r.err, "[error] project.item.generation_failed")) << command << "\n" << r.err;
    };
    // report: cab1's 10 parts are still listed.
    const auto rep = fos({"report", file, "--type", "summary"});
    expectIncomplete(rep, "report");
    EXPECT_TRUE(contains(rep.out, "Total: 10 parts"));
    const auto csv = fos({"report", file, "--type", "parts", "--csv", dir.file("parts.csv")});
    expectIncomplete(csv, "report");
    EXPECT_TRUE(contains(readFile(dir.file("parts.csv")), "cab1/bottom"));
    expectIncomplete(fos({"report", file, "--html", dir.file("s.html"), "cab1"}), "report");
    EXPECT_TRUE(std::filesystem::exists(dir.file("s.html")));
    // cut: the same 9 pieces as the reference project; the summary file carries the notice too.
    const std::string out = dir.file("cut");
    const auto cut = fos({"cut", file, "--out-dir", out});
    expectIncomplete(cut, "cut");
    EXPECT_TRUE(contains(cut.out, "pieces: 9 required, 9 placed, 0 unplaced"));
    EXPECT_TRUE(contains(readFile(out + "/cutting-summary.txt"), "MISSING from this output: cab2."));
    // drawing, labels, cam, export-obj.
    expectIncomplete(fos({"drawing", file, "cab1", "--svg", dir.file("d.svg")}), "drawing");
    EXPECT_TRUE(std::filesystem::exists(dir.file("d.svg")));
    const auto labels = fos({"labels", file, "--svg-dir", dir.file("labels")});
    expectIncomplete(labels, "labels");
    EXPECT_TRUE(contains(labels.out, "Wrote 10 labels"));
    const auto cam = fos({"cam", file, "cab1/side.left", "--face", "A", "--out", dir.file("a.nc")});
    expectIncomplete(cam, "cam");
    EXPECT_TRUE(std::filesystem::exists(dir.file("a.nc")));
    const auto obj = fos({"export-obj", file, dir.file("m.obj")});
    expectIncomplete(obj, "export-obj");
    EXPECT_TRUE(contains(obj.out, "Wrote 10 parts"));
    // The reference project itself has no such notice.
    const auto good = fos({"report", project, "--type", "summary"});
    EXPECT_EQ(good.code, kExitOk);
    EXPECT_FALSE(contains(good.err, "INCOMPLETE"));
}

TEST_F(Cli, OutputIsDeterministic) {
    const auto a = fos({"report", project, "--type", "parts"});
    const auto b = fos({"report", project, "--type", "parts"});
    EXPECT_EQ(a.out, b.out);
    ASSERT_EQ(fos({"drawing", project, "cab1", "--svg", dir.file("1.svg")}).code, kExitOk);
    ASSERT_EQ(fos({"drawing", project, "cab1", "--svg", dir.file("2.svg")}).code, kExitOk);
    EXPECT_EQ(readFile(dir.file("1.svg")), readFile(dir.file("2.svg")));
    ASSERT_EQ(fos({"cut", project, "--out-dir", dir.file("c1")}).code, kExitOk);
    ASSERT_EQ(fos({"cut", project, "--out-dir", dir.file("c2")}).code, kExitOk);
    EXPECT_EQ(readFile(dir.file("c1/mat.generic.pb18-sheet-1.svg")), readFile(dir.file("c2/mat.generic.pb18-sheet-1.svg")));
}

} // namespace
} // namespace fos::cli::test
