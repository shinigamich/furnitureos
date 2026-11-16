// SPDX-License-Identifier: AGPL-3.0-only
/// Regression proof for ADR-0008 (phase 12): every production report of the rectangular
/// fixtures must stay byte-for-byte identical to the output recorded BEFORE contoured-part
/// support was added (tests/golden/rect_fixtures.txt, generated with the phase-11 code).
/// Regenerate only deliberately: FOS_UPDATE_GOLDEN=1 ./fos_production_tests
#include "fos/production/production.hpp"
#include "test_models.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace fos;
using namespace fos::production;

namespace {

std::string diag(const core::Diagnostics& ds) {
    std::string out;
    for (const auto& d : ds) out += d.code + " | " + d.message + " | " + d.entityId + "\n";
    return out;
}

std::string dumpScope(const domain::Model& m, const Scope& scope) {
    std::string s;
    CsvOptions csv;
    csv.crlf = false;
    const PartList pl = partList(m, scope);
    s += "## part list\n" + toCsv(partListTable(pl), csv) + diag(pl.diagnostics);
    const CutList cl = cutList(m, {}, scope);
    s += "## cut list\n" + toCsv(cutListTable(cl), csv) + diag(cl.diagnostics);
    CutListOptions perAssembly;
    perAssembly.groupAcrossAssemblies = false;
    s += "## cut list per assembly\n" + toCsv(cutListTable(cutList(m, perAssembly, scope)), csv);
    const MaterialReport mr = materialReport(m, scope);
    s += "## materials\n" + toCsv(materialTable(mr), csv) + diag(mr.diagnostics);
    const EdgeBandingReport er = edgeBandingReport(m, {}, scope);
    s += "## edge banding\n" + toCsv(edgeBandingTable(er), csv) + diag(er.diagnostics);
    EdgeBandingOptions eo;
    eo.overhangPerEnd = 30;
    eo.wastePercent = 10;
    s += "## edge banding 30/10%\n" + toCsv(edgeBandingTable(edgeBandingReport(m, eo, scope)), csv);
    const HardwareReport hr = hardwareReport(m, scope);
    s += "## hardware\n" + toCsv(hardwareTable(hr), csv) + diag(hr.diagnostics);
    const MachiningSummary ms = machiningSummary(m, {}, scope);
    s += "## machining\n" + toCsv(machiningTable(ms), csv) + diag(ms.diagnostics);
    MachiningOptions all;
    all.includePartsWithoutOperations = true;
    s += "## machining all\n" + toCsv(machiningTable(machiningSummary(m, all, scope)), csv);
    s += "totals " + std::to_string(ms.totalHoles) + " " + std::to_string(ms.totalGrooves) + " " +
         std::to_string(ms.totalPockets) + "\n";
    LabelOptions lo;
    lo.projectName = "Kitchen";
    const LabelSet ls = labels(m, lo, scope);
    s += "## labels\n" + toCsv(labelTable(ls), csv) + diag(ls.diagnostics);
    const auto svg = renderLabelSheetsSvg(ls.labels);
    if (svg)
        for (const auto& page : svg.value()) s += "## label page\n" + page;
    const CostEstimate ce = costEstimate(m, {}, scope);
    s += "## cost\n" + toCsv(costTable(ce), csv) + diag(ce.diagnostics);
    const ProjectSummary ps = projectSummary(m, scope);
    s += "## summary\n" + toCsv(summaryTable(ps), csv);
    std::ostringstream o;
    o.precision(17);
    o << "summary " << ps.partCount << " " << ps.finishedAreaM2 << " " << ps.cutAreaM2 << " "
      << ps.edgeBandingAppliedM << "\n";
    s += o.str();
    return s;
}

std::string dumpAll() {
    std::string s;
    const domain::Model basic = domain::fixtures::basicCabinet();
    s += "# basicCabinet\n" + dumpScope(basic, {});
    const domain::Model two = production::testing::twoCabinets();
    s += "# twoCabinets\n" + dumpScope(two, {});
    Scope cab2;
    cab2.assembly = domain::AssemblyId("cab2");
    s += "# twoCabinets scope cab2\n" + dumpScope(two, cab2);
    for (const auto& a : two.assemblies) {
        HtmlSheetOptions ho;
        ho.projectName = "Kitchen";
        const auto html = productionSheetHtml(two, a.id, ho);
        if (html) s += "# html " + a.id.str() + "\n" + html.value();
    }
    // Broken references: missing material and edge band.
    domain::Model broken = basic;
    auto& parts = broken.assemblies.front().parts;
    parts.front().material = domain::MaterialId("mat.unknown");
    parts.back().edges[0] = domain::EdgeBandId("eb.unknown");
    s += "# broken\n" + dumpScope(broken, {});
    return s;
}

} // namespace

TEST(RectangularGolden, AllReportsUnchangedByContourSupport) {
    const std::string path = std::string(FOS_PRODUCTION_GOLDEN_DIR) + "/rect_fixtures.txt";
    const std::string now = dumpAll();
    if (const char* u = std::getenv("FOS_UPDATE_GOLDEN"); u && std::string(u) == "1") {
        std::ofstream(path, std::ios::binary) << now;
        GTEST_SKIP() << "golden file rewritten: " << path;
    }
    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(in.good()) << "missing golden file " << path;
    std::stringstream buf;
    buf << in.rdbuf();
    const std::string golden = buf.str();
    ASSERT_FALSE(golden.empty());
    // Compare line by line for a readable failure.
    std::istringstream a(golden), b(now);
    std::string la, lb;
    int line = 0;
    while (true) {
        const bool ga = static_cast<bool>(std::getline(a, la));
        const bool gb = static_cast<bool>(std::getline(b, lb));
        ++line;
        if (!ga && !gb) break;
        ASSERT_EQ(la, lb) << "first difference at line " << line;
    }
    EXPECT_EQ(golden, now);
}
