// SPDX-License-Identifier: AGPL-3.0-only
// File > Export job pack: section selection, metadata, cutting-plan SVGs from the Session.
#include "fos/app/job_pack_export.hpp"
#include "test_support.hpp"

namespace fos::app::test {
namespace {

std::size_t count(const std::string& text, const std::string& what) {
    std::size_t n = 0;
    for (auto pos = text.find(what); pos != std::string::npos; pos = text.find(what, pos + 1)) ++n;
    return n;
}

TEST(JobPack, AllSectionsWithCuttingPlansOfTheSession) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    JobPackRequest req;
    req.date = "2026-11-27";
    auto html = jobPackHtml(s, req);
    ASSERT_TRUE(html.ok()) << html.error().message;
    const std::string& h = html.value();
    EXPECT_NE(h.find("2026-11-27"), std::string::npos);
    for (const char* id : {"id=\"cover\"", "id=\"parts\"", "id=\"cutlist\"", "id=\"plans\""})
        EXPECT_NE(h.find(id), std::string::npos) << id;
    // One inline SVG per sheet of the Session's guillotine and nesting plans.
    std::size_t sheets = 0;
    for (const auto& p : s.cuttingPlans().plans)
        if (p.result) sheets += p.result->sheets.size();
    for (const auto& p : s.cuttingPlans().nestingPlans)
        if (p.result) sheets += p.result->sheets.size();
    ASSERT_GT(sheets, 0u);
    EXPECT_EQ(count(h, "<svg"), sheets);
    EXPECT_EQ(count(h, "<?xml"), 0u); // prologs removed by the production module
}

TEST(JobPack, SectionSubsetAndErrors) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    JobPackRequest req;
    req.sections = {production::JobPackSection::Parts, production::JobPackSection::Hardware};
    auto html = jobPackHtml(s, req);
    ASSERT_TRUE(html.ok()) << html.error().message;
    EXPECT_NE(html.value().find("id=\"parts\""), std::string::npos);
    EXPECT_EQ(html.value().find("id=\"plans\""), std::string::npos);
    EXPECT_EQ(count(html.value(), "<svg"), 0u);
    req.sections.clear();
    EXPECT_FALSE(jobPackHtml(s, req).ok()); // empty selection refused by the production module
    EXPECT_EQ(jobPackSectionTitle(production::JobPackSection::Plans), QStringLiteral("Cutting plans"));
}

} // namespace
} // namespace fos::app::test
