// SPDX-License-Identifier: AGPL-3.0-only
// Edge-banding jobs and bought-in view-models, File > Compare with… and the engineering-checks
// switch of the validation.
#include "fos/app/diagnostics_rows.hpp"
#include "fos/app/edge_banding_rows.hpp"
#include "fos/app/project_compare.hpp"
#include "fos/domain/fixtures.hpp"
#include "fos/production/bought_in.hpp"
#include "test_support.hpp"

#include <QSignalSpy>

#include <fstream>
#include <sstream>

namespace fos::app::test {
namespace {

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

const TextNode* findChild(const TextNode& parent, const QString& prefix) {
    for (const auto& c : parent.children)
        if (!c.columns.isEmpty() && c.columns[0].startsWith(prefix)) return &c;
    return nullptr;
}

const TextNode* findTop(const std::vector<TextNode>& nodes, const QString& prefix) {
    for (const auto& n : nodes)
        if (!n.columns.isEmpty() && n.columns[0].startsWith(prefix)) return &n;
    return nullptr;
}

TEST(EdgeBandingRows, BandsPartsEdgesAndTotals) {
    // basicCabinet, overhang 25 mm per end (default):
    //   abs2:  L1 of both sides (720) + bottom, top, shelf (764) = 5 edges, 3732 mm, strips 3732 + 5*50 = 3982.
    //   abs04: W1 + W2 of both sides (560) = 4 edges, 2240 mm, strips 2240 + 4*50 = 2440.
    const auto jobs = production::edgeBandingJobs(domain::fixtures::basicCabinet());
    const auto rows = EdgeBandingText::rows(jobs);
    ASSERT_EQ(rows.bands.size(), 2u);
    const TextNode& abs2 = rows.bands[0];
    EXPECT_TRUE(abs2.columns[0].startsWith(QStringLiteral("ABS 2 mm (eb.abs2)")));
    EXPECT_EQ(abs2.columns[1], QStringLiteral("5 edge(s)"));
    EXPECT_EQ(abs2.columns[4], QStringLiteral("3732"));
    EXPECT_EQ(abs2.columns[5], QStringLiteral("3982"));
    EXPECT_EQ(abs2.children.size(), 5u);
    EXPECT_EQ(rows.bands[1].columns[5], QStringLiteral("2440"));
    // Left side in the abs2 group: only L1 here, 3 banded edges overall -> L1 first (longest, 720):
    // cut edge length = cut length 719.2 (720 - 2 x 0.4 of the W bands), strip 720 + 50 = 770.
    const TextNode* left = nullptr;
    for (const auto& p : abs2.children)
        if (p.columns[0].contains(QStringLiteral("Left side"))) left = &p;
    ASSERT_NE(left, nullptr);
    EXPECT_TRUE(left->columns[0].contains(QStringLiteral("cut 719.2 × 558 × 18 mm")));
    EXPECT_TRUE(left->columns[0].contains(QStringLiteral("another band")));
    ASSERT_EQ(left->children.size(), 1u);
    EXPECT_EQ(left->children[0].columns,
              (QStringList{QString(), QStringLiteral("L1"), QStringLiteral("1/3"), QStringLiteral("719.2"),
                           QStringLiteral("720"), QStringLiteral("770")}));
    EXPECT_TRUE(rows.contour.empty());
    // (3982 + 2440) / 1000 = 6.422 m.
    EXPECT_TRUE(rows.summary.contains(QStringLiteral("strips total 6.422 m"))) << rows.summary.toStdString();
    EXPECT_TRUE(rows.summary.contains(QStringLiteral("Overhang per end: 25 mm")));
}

TEST(EdgeBandingRows, CsvAndSvgExport) {
    const auto jobs = production::edgeBandingJobs(domain::fixtures::basicCabinet());
    const std::string csv = edgeBandingJobsCsv(jobs);
    ASSERT_GE(csv.size(), 3u);
    EXPECT_EQ(csv.substr(0, 3), "\xEF\xBB\xBF");
    EXPECT_NE(csv.find("Band,Band name"), std::string::npos);
    // Header + 9 banded edges (5 abs2 + 4 abs04), CRLF per record.
    std::size_t lines = 0;
    for (std::size_t p = csv.find("\r\n"); p != std::string::npos; p = csv.find("\r\n", p + 2)) ++lines;
    EXPECT_EQ(lines, 10u);

    TempDir dir;
    auto files = exportEdgeBandingSheets(jobs, dir.path / "jobs.svg");
    ASSERT_TRUE(files) << files.error().message;
    ASSERT_GE(files.value().size(), 2u);
    EXPECT_EQ(files.value().front().filename().string().rfind("jobs-eb.abs2", 0), 0u);
    for (const auto& f : files.value()) EXPECT_NE(slurp(f).find("<svg"), std::string::npos) << f;

    // Nothing banded: no files, NotFound.
    Session empty;
    empty.newProject();
    ASSERT_NE(empty.resolved(), nullptr);
    const auto none = projectEdgeBandingJobs(empty.project(), empty.resolved()->model);
    EXPECT_TRUE(none.groups.empty());
    auto r = exportEdgeBandingSheets(none, dir.path / "none.svg");
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, core::ErrorCode::NotFound);
}

TEST(BoughtInText, TranslatedTableAndSummary) {
    auto model = domain::fixtures::basicCabinet();
    domain::Part glass;
    glass.id = domain::PartId("cab1/glass");
    glass.name = "Glass";
    glass.length = 400;
    glass.width = 600;
    glass.thickness = 4;
    glass.material = domain::MaterialId("mat.hdf3");
    glass.custom[domain::kGlassPaneKey] = "true";
    model.assemblies[0].parts.push_back(glass);
    const auto list = production::boughtInList(model);
    const auto table = BoughtInText::table(list);
    ASSERT_EQ(table.header.size(), 10u);
    EXPECT_EQ(table.header[0], "Item");
    ASSERT_EQ(table.rows.size(), 1u);
    EXPECT_EQ(table.rows[0][0], "Glass pane");
    EXPECT_EQ(table.rows[0][5], "1");
    // 400 x 600 mm = 0.24 m2.
    EXPECT_EQ(BoughtInText::summary(list), QStringLiteral("1 bought-in item(s) to order, total area 0.24 m2"));
    EXPECT_NE(boughtInCsv(list).find("Glass pane"), std::string::npos);
    EXPECT_EQ(BoughtInText::summary(production::boughtInList(domain::fixtures::basicCabinet())),
              QStringLiteral("No bought-in items."));
}

TEST(ProjectCompare, GroupedTreeAndExports) {
    TempDir dir;
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    const auto file = dir.path / "a.fosp";
    ASSERT_TRUE(s.saveAs(file).ok());
    // Identical.
    auto same = compareWithFile(s.project(), file);
    ASSERT_TRUE(same) << same.error().message;
    auto tree = CompareText::tree(same.value());
    ASSERT_EQ(tree.size(), 1u);
    EXPECT_EQ(tree[0].columns[0], QStringLiteral("No differences"));

    // Current (A) has one more cabinet than the file (B): the item is "removed" from A to B,
    // and so are its generated parts.
    ASSERT_TRUE(s.addFromTemplate(TemplateKind::WallCabinet).ok());
    auto diff = compareWithFile(s.project(), file);
    ASSERT_TRUE(diff) << diff.error().message;
    ASSERT_EQ(diff.value().itemsRemoved.size(), 1u);
    tree = CompareText::tree(diff.value());
    const TextNode* items = findTop(tree, QStringLiteral("Items ("));
    ASSERT_NE(items, nullptr);
    EXPECT_NE(findChild(*items, QStringLiteral("Removed: ")), nullptr);
    const TextNode* model = findTop(tree, QStringLiteral("Production model ("));
    ASSERT_NE(model, nullptr);
    const TextNode* removed = findChild(*model, QStringLiteral("Parts removed ("));
    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(removed->children.size(), diff.value().model->removed.size());
    EXPECT_EQ(removed->children[0].columns[2], QStringLiteral("—"));

    const auto json = formats::Json::parse(compareJson(diff.value()));
    EXPECT_FALSE(json.at("identical").get<bool>());
    EXPECT_TRUE(json.contains("model"));
    EXPECT_EQ(compareText(diff.value()), project::diffText(diff.value()));

    EXPECT_EQ(CompareText::value(std::nullopt), QStringLiteral("—"));
    EXPECT_EQ(CompareText::value(formats::Json("Oak")), QStringLiteral("Oak"));
    EXPECT_EQ(CompareText::value(formats::Json(18.5)), QStringLiteral("18.5"));

    EXPECT_FALSE(compareWithFile(s.project(), dir.path / "missing.fosp"));
}

TEST(EngineeringChecks, SessionSwitchesValidationOptions) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::TallCabinet).ok());
    EXPECT_FALSE(s.engineeringChecks());
    EXPECT_FALSE(s.validationOptions().checkEngineering);
    const std::size_t before = s.validationReport().diagnostics.size();
    QSignalSpy spy(&s, &Session::validationChanged);
    s.setEngineeringChecks(true);
    s.setEngineeringChecks(true); // no change, no signal
    EXPECT_EQ(spy.count(), 1);
    EXPECT_TRUE(s.validationOptions().checkEngineering);
    validation::ValidationOptions options;
    options.checkEngineering = true;
    const auto expected = validation::validateModel(s.resolved()->model, options);
    EXPECT_EQ(s.validationReport().diagnostics.size(), expected.diagnostics.size());
    EXPECT_GE(expected.diagnostics.size(), before);
    std::size_t engineering = 0;
    for (const auto& row : diagnosticRows(*s.resolved(), s.validationReport())) engineering += isEngineeringFinding(row);
    EXPECT_EQ(summarize(diagnosticRows(*s.resolved(), s.validationReport())).engineering, engineering);

    DiagnosticRow row;
    row.source = DiagnosticSource::Validation;
    row.code = "validation.engineering.shelf_sag";
    EXPECT_TRUE(isEngineeringFinding(row));
    row.code = "validation.part.too_small";
    EXPECT_FALSE(isEngineeringFinding(row));
}

} // namespace
} // namespace fos::app::test
