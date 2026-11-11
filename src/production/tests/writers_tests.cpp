// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/production.hpp"
#include "test_models.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

using namespace fos;
using namespace fos::production;

TEST(Csv, FieldQuotingRfc4180) {
    EXPECT_EQ(csvField("plain"), "plain");
    EXPECT_EQ(csvField(""), "");
    EXPECT_EQ(csvField("a,b"), "\"a,b\"");
    EXPECT_EQ(csvField("a;b"), "a;b");             // ';' is ordinary when the separator is ','
    EXPECT_EQ(csvField("a;b", ';'), "\"a;b\"");
    EXPECT_EQ(csvField("a,b", ';'), "a,b");
    EXPECT_EQ(csvField("say \"hi\""), "\"say \"\"hi\"\"\"");
    EXPECT_EQ(csvField("line1\nline2"), "\"line1\nline2\"");
    EXPECT_EQ(csvField("cr\rlf"), "\"cr\rlf\"");
    EXPECT_EQ(csvField(" padded "), "\" padded \"");
    EXPECT_EQ(csvField("Полиця верхня"), "Полиця верхня"); // UTF-8 passes through unquoted
    EXPECT_EQ(csvField("Полиця, \"біла\""), "\"Полиця, \"\"біла\"\"\"");
}

TEST(Csv, TableSerialisation) {
    Table t;
    t.header = {"Назва", "Size, mm"};
    t.rows = {{"Дверцята \"A\"", "719.2"}, {"two\nlines", ""}};
    EXPECT_EQ(toCsv(t), "Назва,\"Size, mm\"\r\n\"Дверцята \"\"A\"\"\",719.2\r\n\"two\nlines\",\r\n");

    CsvOptions o;
    o.separator = ';';
    o.utf8Bom = true;
    o.crlf = false;
    EXPECT_EQ(toCsv(t, o), "\xEF\xBB\xBFНазва;Size, mm\n\"Дверцята \"\"A\"\"\";719.2\n\"two\nlines\";\n");

    o = {};
    o.writeHeader = false;
    EXPECT_EQ(toCsv(Table{{"h"}, {{"x"}}}, o), "x\r\n");
    EXPECT_EQ(toCsv(Table{{"h"}, {}}), "h\r\n");
    o = {};
    o.separator = '"'; // unusable -> comma
    EXPECT_EQ(toCsv(Table{{"a", "b"}, {}}, o), "a,b\r\n");
}

TEST(Csv, PartListOfBasicCabinet) {
    const auto csv = toCsv(partListTable(partList(domain::fixtures::basicCabinet())));
    std::istringstream in(csv);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(in, line)) lines.push_back(line);
    ASSERT_EQ(lines.size(), 7u); // header + 6 parts
    EXPECT_EQ(lines[0], "No.,Assembly,Part ID,Name,Role,Material,Length,Width,Thickness,Cut length,Cut width,"
                        "Grain,Edge L1,Edge L2,Edge W1,Edge W2,Operations\r");
    EXPECT_EQ(lines[1], "1,Base cabinet 800,cab1/side.left,Left side,Side,Particleboard 18 mm,720,560,18,719.2,"
                        "558,along length,ABS 2 mm,,ABS 0.4 mm,ABS 0.4 mm,2\r");
    EXPECT_EQ(lines[6], "6,Base cabinet 800,cab1/back,Back panel,Back,HDF 3 mm,796,716,3,796,716,none,,,,,0\r");
}

TEST(Csv, DecimalsAreConfigurableAndLocaleIndependent) {
    auto m = domain::fixtures::basicCabinet();
    m.assemblies[0].parts[0].length = 720.26; // cut length 719.46
    NumberFormat nf;
    EXPECT_EQ(partListTable(partList(m), {}, nf).rows[0][9], "719.5"); // default 0.1 mm
    nf.length = 2;
    EXPECT_EQ(partListTable(partList(m), {}, nf).rows[0][9], "719.46");
    nf.length = 0;
    EXPECT_EQ(partListTable(partList(m), {}, nf).rows[0][9], "719");

    // Semicolon CSV still uses '.' as the decimal separator (no locale involved).
    CsvOptions o;
    o.separator = ';';
    const auto csv = toCsv(materialTable(materialReport(domain::fixtures::basicCabinet())), o);
    EXPECT_NE(csv.find(";2.0746;2.0663;"), std::string::npos); // 2.07464 and 2.0662832 at 4 decimals
    nf = {};
    nf.area = 2;
    EXPECT_EQ(materialTable(materialReport(domain::fixtures::basicCabinet()), {}, nf).rows[0][3], "2.07");
}

TEST(Tables, LocalisedHeadersAndTerms) {
    Localisation uk;
    uk.partList.name = "Назва";
    uk.partList.material = "Матеріал";
    uk.cutList.quantity = "К-сть";
    uk.terms.grainAlongLength = "вздовж";
    uk.terms.yes = "так";
    uk.terms.no = "ні";
    uk.terms.roles[domain::PartRole::Side] = "Боковина";
    const auto m = domain::fixtures::basicCabinet();
    const auto pt = partListTable(partList(m), uk);
    EXPECT_EQ(pt.header[3], "Назва");
    EXPECT_EQ(pt.header[5], "Матеріал");
    EXPECT_EQ(pt.header[0], "No."); // untouched default
    EXPECT_EQ(pt.rows[0][4], "Боковина");
    EXPECT_EQ(pt.rows[2][4], "Bottom"); // role without a translation falls back to the domain name
    EXPECT_EQ(pt.rows[0][11], "вздовж");
    const auto ct = cutListTable(cutList(m), uk);
    EXPECT_EQ(ct.header[4], "К-сть");
    EXPECT_EQ(ct.rows[0][8], "так"); // HDF back may rotate
    EXPECT_EQ(ct.rows[1][8], "ні");
    EXPECT_EQ(ct.rows[1][4], "2");
    EXPECT_EQ(ct.rows[1][13], "Bottom / Top");
    EXPECT_EQ(ct.rows[1][15], "3, 4");
    EXPECT_EQ(ct.rows[1][16], "cab1/bottom, cab1/top");
}

TEST(Tables, EveryReportHasARectangularTable) {
    const auto m = production::testing::twoCabinets();
    const std::vector<Table> tables = {
        partListTable(partList(m)),         cutListTable(cutList(m)),
        materialTable(materialReport(m)),   edgeBandingTable(edgeBandingReport(m)),
        hardwareTable(hardwareReport(m)),   machiningTable(machiningSummary(m)),
        labelTable(labels(m)),              costTable(costEstimate(m)),
        summaryTable(projectSummary(m))};
    for (const auto& t : tables) {
        EXPECT_FALSE(t.header.empty());
        EXPECT_FALSE(t.rows.empty());
        for (const auto& row : t.rows) EXPECT_EQ(row.size(), t.header.size());
        EXPECT_FALSE(toCsv(t).empty());
    }
    // Spot checks against the hand-computed fixture values (two cabinets).
    EXPECT_EQ(tables[3].rows[0][4], "7.464"); // abs2 applied: 2 x 3.732 m
    EXPECT_EQ(tables[3].rows[0][3], "10");
    EXPECT_EQ(tables[4].rows[0][3], "8");
    EXPECT_EQ(tables[4].rows[0][5], "0.4");
    EXPECT_EQ(tables[4].rows[0][7], "dimensions not verified");
    EXPECT_EQ(tables[5].rows[0][4], "2 x 5 @ B");
    EXPECT_EQ(tables[6].rows[9][12], "4/4");
    EXPECT_EQ(tables[2].rows[0][7], "1"); // 4.1325664 m2 of particleboard still below one 5.796 m2 sheet
    EXPECT_NE(tables[2].rows[0][10].find("area lower bound"), std::string::npos);
    EXPECT_NE(tables[2].header[7].find("lower bound"), std::string::npos);
}

TEST(Csv, WriteTextFile) {
    const auto dir = std::filesystem::temp_directory_path() / "fos_production_tests";
    std::filesystem::create_directories(dir);
    const auto path = dir / "parts.csv";
    CsvOptions o;
    o.utf8Bom = true;
    Table t{{"Назва"}, {{"Полиця"}}};
    const std::string content = toCsv(t, o);
    ASSERT_TRUE(writeTextFile(path, content).ok());
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    EXPECT_EQ(ss.str(), content); // byte-exact, CRLF untouched
    EXPECT_EQ(ss.str().substr(0, 3), "\xEF\xBB\xBF");
    std::filesystem::remove(path);

    const auto bad = writeTextFile(dir / "no_such_dir" / "x.csv", "x");
    ASSERT_FALSE(bad.ok());
    EXPECT_EQ(bad.error().code, core::ErrorCode::IoError);
    EXPECT_NE(bad.error().message.find("no_such_dir"), std::string::npos);
}

TEST(Html, EscapeXml) {
    EXPECT_EQ(escapeXml("a<b>&\"c'"), "a&lt;b&gt;&amp;&quot;c&#39;");
    EXPECT_EQ(escapeXml("Шафа №2"), "Шафа №2");
    EXPECT_EQ(escapeXml(std::string("a\x01" "b\tc\n", 6)), "a b\tc\n");
    EXPECT_EQ(tableHtml(Table{{"<h>"}, {{"1 & 2"}}}),
              "<table>\n<thead><tr><th>&lt;h&gt;</th></tr></thead>\n<tbody>\n<tr><td>1 &amp; 2</td></tr>\n"
              "</tbody>\n</table>\n");
}

TEST(Html, ProductionSheetForOneAssembly) {
    auto m = production::testing::twoCabinets();
    m.assemblies[1].name = "Тумба <script>alert(1)</script>";
    m.assemblies[1].parts[0].name = "Боковина & Co";
    HtmlSheetOptions o;
    o.projectName = "Кухня \"Літо\"";
    o.text.lang = "uk";
    o.text.title = "Виробничий лист";
    o.text.cutList = "Карта розкрою";
    o.localisation.cutList.quantity = "К-сть";
    const auto r = productionSheetHtml(m, domain::AssemblyId("cab2"), o);
    ASSERT_TRUE(r.ok());
    const std::string& html = r.value();
    EXPECT_EQ(html.rfind("<!DOCTYPE html>\n<html lang=\"uk\">", 0), 0u);
    EXPECT_NE(html.find("<meta charset=\"utf-8\">"), std::string::npos);
    EXPECT_NE(html.find("<h1>Виробничий лист</h1>"), std::string::npos);
    EXPECT_NE(html.find("<h2>Карта розкрою</h2>"), std::string::npos);
    EXPECT_NE(html.find("<h2>Part list</h2>"), std::string::npos);
    EXPECT_NE(html.find("<h2>Edge banding</h2>"), std::string::npos);
    EXPECT_NE(html.find("<h2>Hardware</h2>"), std::string::npos);
    EXPECT_NE(html.find("<th>К-сть</th>"), std::string::npos);
    EXPECT_NE(html.find("Кухня &quot;Літо&quot;"), std::string::npos);
    // Escaped, never executable.
    EXPECT_EQ(html.find("<script"), std::string::npos);
    EXPECT_NE(html.find("Тумба &lt;script&gt;alert(1)&lt;/script&gt;"), std::string::npos);
    EXPECT_NE(html.find("<td>Боковина &amp; Co</td>"), std::string::npos);
    // Only cab2: its part ids are present, cab1's are not; part numbers stay model-wide (7..12).
    EXPECT_NE(html.find("cab2/side.left"), std::string::npos);
    EXPECT_EQ(html.find("cab1/"), std::string::npos);
    EXPECT_NE(html.find("<tr><td>7</td>"), std::string::npos);
    EXPECT_NE(html.find("800 x 563 x 720"), std::string::npos);
    EXPECT_NE(html.find("<td>3.732</td>"), std::string::npos); // abs2 applied length of ONE cabinet
    EXPECT_NE(html.find("Shelf support pin 5 mm (generic)"), std::string::npos);
    // Self-contained: no external resources.
    EXPECT_EQ(html.find("http"), std::string::npos);
    EXPECT_EQ(html.find("<link"), std::string::npos);
    EXPECT_NE(html.find("@media print"), std::string::npos);
    EXPECT_EQ(html.find("<h2>Warnings</h2>"), std::string::npos); // clean model

    const auto missing = productionSheetHtml(m, domain::AssemblyId("nope"));
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().code, core::ErrorCode::NotFound);
    EXPECT_NE(missing.error().message.find("'nope'"), std::string::npos);
}
