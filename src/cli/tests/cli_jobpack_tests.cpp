// SPDX-License-Identifier: AGPL-3.0-only
// fos jobpack (one self-contained HTML job pack) and `fos batch report --type boughtin`.
#include "cli_test_support.hpp"

#include "fos/project/file.hpp"

namespace fos::cli::test {
namespace {

/// Base cabinet whose item is named "<script>alert(1)</script>" and whose front is a glass-panel
/// frame front (the pane is a bought-in item).
void makeGlassProject(const TempDir& dir, const std::string& name) {
    ASSERT_EQ(newBaseCabinet(dir.file(name)).code, 0);
    const auto file = dir.path / name;
    auto loaded = project::loadProject(file);
    ASSERT_TRUE(loaded) << loaded.error().message;
    project::Project p = loaded.value().project;
    domain::Material g;
    g.id = domain::MaterialId("glass.float4");
    g.name = "Float glass 4 mm";
    g.kind = domain::MaterialKind::Glass;
    g.thickness = 4;
    p.libraries.materials.push_back(g);
    ASSERT_FALSE(p.items.empty());
    auto* cab = p.items[0].cabinet();
    ASSERT_TRUE(cab != nullptr && cab->interior.front.has_value());
    cab->name = "<script>alert(1)</script>";
    auto& f = *cab->interior.front;
    f.style = furniture::FrontStyle::FramePanel;
    f.frame.joint = furniture::FrameJoint::Dowel;
    f.frame.panelKind = furniture::FramePanelKind::Glass;
    f.frame.panelMaterial = g.id;
    formats::SaveOptions so;
    so.backupCount = 0;
    ASSERT_TRUE(project::saveProject(file, p, so));
}

std::size_t count(const std::string& text, const std::string& needle) {
    std::size_t n = 0;
    for (auto pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + needle.size())) ++n;
    return n;
}

} // namespace

TEST(CliJobpack, FullPackIsSelfContainedEscapedAndDeterministic) {
    TempDir dir;
    makeGlassProject(dir, "k.fosp");
    const std::string file = dir.file("k.fosp");
    auto r = fos({"jobpack", file, "-o", dir.file("job.html"), "--date", "2026-11-26"});
    ASSERT_EQ(r.code, 0) << r.out << r.err;
    EXPECT_TRUE(contains(r.out, "Wrote job pack")) << r.out;
    const std::string h = readFile(dir.path / "job.html");
    for (const char* id : {"cover", "parts", "cutlist", "boughtin", "edgebanding", "hardware", "packing", "assembly",
                           "plans"})
        EXPECT_TRUE(contains(h, "<section id=\"" + std::string(id) + "\">")) << id;
    EXPECT_TRUE(contains(h, "<strong>2026-11-26</strong>"));
    // Bought-in glass pane listed.
    EXPECT_TRUE(contains(h, "<td>Glass pane</td><td>Float glass 4 mm</td>")) << h.substr(0, 4000);
    // Malicious item name escaped everywhere, no script element at all.
    EXPECT_FALSE(contains(h, "<script"));
    EXPECT_TRUE(contains(h, "&lt;script&gt;alert(1)&lt;/script&gt;"));
    // Cutting plans inline, without XML declarations; nothing external.
    EXPECT_GE(count(h, "<svg"), 1u);
    EXPECT_EQ(count(h, "<svg"), count(h, "</svg>"));
    EXPECT_FALSE(contains(h, "<?xml"));
    EXPECT_FALSE(contains(h, "<link"));
    EXPECT_FALSE(contains(h, " src="));

    ASSERT_EQ(fos({"jobpack", file, "--output", dir.file("job2.html"), "--date", "2026-11-26"}).code, 0);
    EXPECT_EQ(readFile(dir.path / "job2.html"), h);
}

TEST(CliJobpack, SectionsAndNoPlans) {
    TempDir dir;
    ASSERT_EQ(newBaseCabinet(dir.file("k.fosp")).code, 0);
    auto r = fos({"jobpack", dir.file("k.fosp"), "-o", dir.file("j.html"), "--sections", "hardware,cover,plans",
                  "--no-plans", "--date", "x"});
    ASSERT_EQ(r.code, 0) << r.err;
    const std::string h = readFile(dir.path / "j.html");
    EXPECT_EQ(count(h, "<section id="), 2u);
    EXPECT_LT(h.find("<section id=\"cover\">"), h.find("<section id=\"hardware\">"));
    EXPECT_FALSE(contains(h, "<svg"));
}

TEST(CliJobpack, UsageErrors) {
    TempDir dir;
    ASSERT_EQ(newBaseCabinet(dir.file("k.fosp")).code, 0);
    EXPECT_EQ(fos({"jobpack", dir.file("k.fosp")}).code, 2); // -o missing
    const auto bad = fos({"jobpack", dir.file("k.fosp"), "-o", dir.file("j.html"), "--sections", "cover,nope"});
    EXPECT_EQ(bad.code, 2);
    EXPECT_TRUE(contains(bad.err, "unknown section 'nope'")) << bad.err;
    EXPECT_EQ(fos({"jobpack", dir.file("k.fosp"), "-o", dir.file("j.html"), "--sections", "plans", "--no-plans"}).code,
              2);
    EXPECT_FALSE(std::filesystem::exists(dir.path / "j.html"));
    EXPECT_EQ(fos({"jobpack", "--help"}).code, 0);
}

TEST(CliBatch, ReportBoughtIn) {
    TempDir dir;
    makeGlassProject(dir, "k.fosp");
    const std::string batch = dir.file("w.fosbatch");
    ASSERT_EQ(fos({"batch", "new", batch, "--name", "W"}).code, 0);
    ASSERT_EQ(fos({"batch", "add", batch, dir.file("k.fosp"), "--qty", "2"}).code, 0);
    const auto r = fos({"batch", "report", batch, "--type", "boughtin"});
    EXPECT_EQ(r.code, 0) << r.out << r.err;
    EXPECT_TRUE(contains(r.out, "Glass pane")) << r.out;
    EXPECT_TRUE(contains(r.out, "Float glass 4 mm")) << r.out;
    const auto csv = fos({"batch", "report", batch, "--type", "boughtin", "--csv"});
    EXPECT_EQ(csv.code, 0) << csv.err;
    EXPECT_TRUE(contains(csv.out, "Glass pane")) << csv.out;
    EXPECT_EQ(fos({"batch", "report", batch, "--type", "nope"}).code, 2);
}

} // namespace fos::cli::test
