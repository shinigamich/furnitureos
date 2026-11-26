// SPDX-License-Identifier: AGPL-3.0-only
// Job pack HTML (fos/production/job_pack.hpp): sections, escaping of user text, bought-in
// items, inline cutting-plan SVG, determinism and option validation.
#include "fos/production/html.hpp"
#include "fos/production/job_pack.hpp"
#include "test_models.hpp"

#include <gtest/gtest.h>

using namespace fos;
using namespace fos::production;

namespace {

std::size_t occurrences(const std::string& text, const std::string& needle) {
    std::size_t n = 0;
    for (auto pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + needle.size())) ++n;
    return n;
}

/// basicCabinet() with a malicious assembly name and one extra glass pane (bought-in) 400 x 300 x 4
/// of material "g4", plus a part named "<script>alert(1)</script>".
domain::Model jobModel() {
    domain::Model m = domain::fixtures::basicCabinet();
    domain::Material g;
    g.id = domain::MaterialId("g4");
    g.name = "Float glass 4";
    g.kind = domain::MaterialKind::Glass;
    g.thickness = 4;
    m.materials.push_back(g);
    auto& a = m.assemblies.front();
    a.name = "Cab <b>&\"1\"";
    domain::Part pane = production::testing::panel("pane", "Door glass", 400, 300, 4, "g4");
    pane.custom[domain::kGlassPaneKey] = "true";
    a.parts.push_back(pane);
    domain::Part evil = a.parts.front();
    evil.id = domain::PartId("evil");
    evil.name = "<script>alert(1)</script>";
    a.parts.push_back(evil);
    return m;
}

} // namespace

TEST(JobPack, SectionNamesRoundTrip) {
    const auto all = allJobPackSections();
    ASSERT_EQ(all.size(), 10u);
    for (const auto s : all) EXPECT_EQ(jobPackSectionFromString(toString(s)), s);
    EXPECT_FALSE(jobPackSectionFromString("Cover").has_value());
    EXPECT_FALSE(jobPackSectionFromString("").has_value());
}

TEST(JobPack, AllSectionsPresentInDocumentOrder) {
    JobPackOptions o;
    o.projectName = "Kitchen";
    o.date = "2026-11-26";
    o.cuttingPlans = {{"Board - sheet 1",
                       "<?xml version=\"1.0\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10mm\"><rect/></svg>\n"}};
    o.extraDiagnostics = {{core::Severity::Warning, "x.extra", "Extra finding.", ""}};
    auto r = jobPackHtml(jobModel(), o);
    ASSERT_TRUE(r) << r.error().message;
    const std::string& h = r.value();
    std::size_t last = 0;
    for (const char* id : {"cover", "parts", "cutlist", "boughtin", "edgebanding", "hardware", "packing", "assembly",
                           "plans", "warnings"}) {
        const auto pos = h.find("<section id=\"" + std::string(id) + "\">");
        ASSERT_NE(pos, std::string::npos) << id;
        EXPECT_GT(pos, last) << id;
        last = pos;
        if (std::string(id) != "cover") {
            EXPECT_NE(h.find("<a href=\"#" + std::string(id) + "\">"), h.npos) << id;
        }
    }
    EXPECT_EQ(h.rfind("<!DOCTYPE html>", 0), 0u);
    EXPECT_NE(h.find("@page{size:A4 landscape"), std::string::npos);
    EXPECT_NE(h.find("break-before:page"), std::string::npos);
    EXPECT_NE(h.find("<td>Date</td><td><strong>2026-11-26</strong>"), std::string::npos);
    // Self-contained: no script, no external references.
    EXPECT_EQ(h.find("<script"), std::string::npos);
    EXPECT_EQ(h.find("<link"), std::string::npos);
    EXPECT_EQ(occurrences(h, "src="), 0u);
    // Inline SVG without its XML declaration.
    EXPECT_NE(h.find("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10mm\"><rect/></svg>"), std::string::npos);
    EXPECT_EQ(h.find("<?xml"), std::string::npos);
    EXPECT_NE(h.find("Extra finding."), std::string::npos);
    // The glass pane is bought-in: no cut-list group for its material.
    EXPECT_EQ(h.find("Float glass 4 (g4) - 4 mm</h3>"), std::string::npos);
}

TEST(JobPack, EscapesUserText) {
    JobPackOptions o;
    o.projectName = "P<i>&";
    auto r = jobPackHtml(jobModel(), o);
    ASSERT_TRUE(r) << r.error().message;
    const std::string& h = r.value();
    EXPECT_EQ(h.find("<script>"), std::string::npos);
    EXPECT_NE(h.find("&lt;script&gt;alert(1)&lt;/script&gt;"), std::string::npos);
    EXPECT_EQ(h.find("<b>"), std::string::npos);
    EXPECT_NE(h.find("Cab &lt;b&gt;&amp;&quot;1&quot;"), std::string::npos);
    EXPECT_NE(h.find("<title>Job pack - P&lt;i&gt;&amp;</title>"), std::string::npos);
}

TEST(JobPack, BoughtInListedAndCounted) {
    auto r = jobPackHtml(jobModel(), {});
    ASSERT_TRUE(r);
    const std::string& h = r.value();
    const auto start = h.find("<section id=\"boughtin\">");
    const auto end = h.find("</section>", start);
    ASSERT_NE(start, std::string::npos);
    const std::string sec = h.substr(start, end - start);
    // Item, Material, Width, Height, Thickness, Qty, Area each = 0.4 * 0.3 = 0.12 m2.
    EXPECT_NE(sec.find("<td>Glass pane</td><td>Float glass 4</td><td>400</td><td>300</td><td>4</td><td>1</td>"
                       "<td>0.12</td>"),
              std::string::npos)
        << sec;
    EXPECT_NE(h.find("<td>Bought-in items</td><td><strong>1 (0.12 m2)</strong>"), std::string::npos);
}

TEST(JobPack, ProductionSheetNowListsBoughtIn) {
    const auto m = jobModel();
    auto r = productionSheetHtml(m, m.assemblies.front().id);
    ASSERT_TRUE(r);
    EXPECT_NE(r.value().find("<h2>Bought-in items</h2>"), std::string::npos);
    EXPECT_NE(r.value().find("<td>Glass pane</td>"), std::string::npos);
}

TEST(JobPack, Deterministic) {
    JobPackOptions o;
    o.projectName = "Kitchen";
    const auto m = jobModel();
    auto a = jobPackHtml(m, o);
    auto b = jobPackHtml(m, o);
    ASSERT_TRUE(a && b);
    EXPECT_EQ(a.value(), b.value());
}

TEST(JobPack, SectionSelectionAndQuantities) {
    JobPackOptions o;
    o.sections = {JobPackSection::Hardware, JobPackSection::Cover, JobPackSection::Cover};
    o.itemQuantities[domain::fixtures::basicCabinet().assemblies.front().id] = 3;
    auto r = jobPackHtml(jobModel(), o);
    ASSERT_TRUE(r);
    const std::string& h = r.value();
    EXPECT_EQ(occurrences(h, "<section id="), 2u);
    EXPECT_LT(h.find("<section id=\"cover\">"), h.find("<section id=\"hardware\">"));
    EXPECT_EQ(h.find("<section id=\"parts\">"), std::string::npos);
    EXPECT_NE(h.find("</td><td>3</td>"), std::string::npos); // Qty column of the item row.
}

TEST(JobPack, PlansSectionWithoutImagesSaysNone) {
    JobPackOptions o;
    o.sections = {JobPackSection::Plans};
    auto r = jobPackHtml(production::testing::smallModel(), o);
    ASSERT_TRUE(r);
    EXPECT_NE(r.value().find("<p class=\"empty\">none</p>"), std::string::npos);
}

TEST(JobPack, InvalidOptionsAreErrors) {
    const auto m = jobModel();
    JobPackOptions o;
    o.sections.clear();
    EXPECT_FALSE(jobPackHtml(m, o));
    o = {};
    o.itemQuantities[domain::AssemblyId("cab1")] = 0;
    auto q = jobPackHtml(m, o);
    ASSERT_FALSE(q);
    EXPECT_NE(q.error().message.find("'cab1' is 0"), std::string::npos);
    o = {};
    o.cuttingPlans = {{"bad", "<html></html>"}};
    EXPECT_FALSE(jobPackHtml(m, o));
    o = {};
    o.packing.maxPackageWeightKg = -1;
    EXPECT_FALSE(jobPackHtml(m, o));
    o.sections = {JobPackSection::Cover}; // packing not included: its options are not used
    EXPECT_TRUE(jobPackHtml(m, o));
}
