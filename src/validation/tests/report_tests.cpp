// SPDX-License-Identifier: AGPL-3.0-only
#include "test_support.hpp"

namespace fos::validation::test {
namespace {

ValidationReport mixed() {
    domain::Model m = domain::fixtures::basicCabinet();
    part(m, "cab1/shelf.1").length = 767;  // error (interference)
    part(m, "cab1/back").name = "Back | HDF"; // pipe must be escaped in Markdown
    m.assemblies[0].parts.push_back(domain::fixtures::basicCabinet().assemblies[0].parts[5]);
    m.assemblies[0].parts.back().id = domain::PartId("cab1/back.2");
    m.assemblies[0].parts.back().name.clear(); // warning (unnamed) + interference with the back
    return validateModel(m);
}

TEST(Report, CountsFiltersAndText) {
    const ValidationReport r = mixed();
    EXPECT_EQ(r.counts.errors, 2u) << toText(r);
    EXPECT_EQ(r.counts.warnings, 1u) << toText(r);
    EXPECT_EQ(r.counts.infos, 1u) << toText(r);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.toDiagnostics().size(), 4u);
    EXPECT_EQ(r.toDiagnostics()[0].code, r.diagnostics[0].code);
    EXPECT_EQ(findingsWithSeverity(r, core::Severity::Warning).size(), 1u);
    EXPECT_EQ(findingsWithCode(r, "validation.interference.parts").size(), 2u);
    // side.right appears only as related id of the shelf finding.
    EXPECT_EQ(findingsForEntity(r, "cab1/side.right").size(), 1u);
    EXPECT_STREQ(toString(core::Severity::Info), "info");

    const std::string text = toText(r);
    EXPECT_EQ(text.substr(0, text.find('\n')),
              "Validation FAILED: 2 error(s), 1 warning(s), 1 info(s); 7 part(s) in 1 assembly(ies), 7 check(s) run.");
    EXPECT_NE(text.find("[error] validation.interference.parts (cab1/shelf.1): 'Shelf' penetrates 'Right side' by 3 mm "
                        "along X.\n"),
              std::string::npos)
        << text;
}

TEST(Report, Markdown) {
    const std::string md = toMarkdown(mixed());
    EXPECT_EQ(md.rfind("# Validation report\n", 0), 0u);
    EXPECT_NE(md.find("## Errors (2)"), std::string::npos);
    EXPECT_NE(md.find("## Warnings (1)"), std::string::npos);
    EXPECT_NE(md.find("## Information (1)"), std::string::npos);
    EXPECT_NE(md.find("'Back \\| HDF'"), std::string::npos) << md;
    EXPECT_NE(md.find("| Possible part pairs | 21 |"), std::string::npos) << md; // 7 * 6 / 2
    EXPECT_LT(md.find("## Errors"), md.find("## Warnings"));
}

TEST(Report, EmptyModel) {
    const ValidationReport r = validateModel(domain::Model{});
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.diagnostics.empty());
    EXPECT_EQ(toText(r), "Validation passed: 0 error(s), 0 warning(s), 0 info(s); 0 part(s) in 0 assembly(ies), 7 "
                         "check(s) run.\n");
}

} // namespace
} // namespace fos::validation::test
