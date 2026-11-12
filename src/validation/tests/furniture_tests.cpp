// SPDX-License-Identifier: AGPL-3.0-only
/// Cross-check with cabinets produced by the parametric generator (module furniture).
/// Built only when fos::furniture is configured (test-only dependency).
#include "test_support.hpp"

#include "fos/furniture/generate.hpp"
#include "fos/furniture/templates.hpp"
#include "fos/hardware/catalog.hpp"

#include <set>

namespace fos::validation::test {
namespace {
using furniture::CabinetDefinition;
namespace templates = furniture::templates;

furniture::Libraries libs() {
    const domain::Model m = domain::fixtures::basicCabinet();
    furniture::Libraries l;
    l.materials = m.materials;
    l.edgeBands = m.edgeBands;
    l.hardware = hardware::builtinCatalog();
    return l;
}

templates::TemplateMaterials mats() {
    return {domain::MaterialId("mat.pb18"), domain::MaterialId("mat.hdf3"), {}, domain::EdgeBandId("eb.abs2"),
            domain::EdgeBandId("eb.abs04")};
}

domain::Model generated(const CabinetDefinition& d) {
    auto r = furniture::generate(d, libs());
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    if (!r.ok()) return {};
    return furniture::toModel(libs(), {r.value().assembly});
}

struct Case {
    const char* name;
    CabinetDefinition (*make)(domain::AssemblyId, const templates::TemplateMaterials&);
};

class Templates : public ::testing::TestWithParam<Case> {};

TEST_P(Templates, GeneratedCabinetHasNoErrors) {
    const domain::Model m = generated(GetParam().make(domain::AssemblyId("c1"), mats()));
    ASSERT_EQ(m.assemblies.size(), 1u);
    const ValidationReport r = validateModel(m);
    EXPECT_EQ(r.counts.errors, 0u) << GetParam().name << "\n" << toText(r);
    EXPECT_EQ(r.counts.warnings, 0u) << GetParam().name << "\n" << toText(r);
    EXPECT_GT(r.stats.parts, 5u);
}

INSTANTIATE_TEST_SUITE_P(All, Templates,
                         ::testing::Values(Case{"base", &templates::baseCabinet}, Case{"wall", &templates::wallCabinet},
                                           Case{"tall", &templates::tallCabinet},
                                           Case{"drawer", &templates::drawerUnit},
                                           Case{"wardrobe", &templates::wardrobeSection},
                                           Case{"shelving", &templates::openShelving}),
                         [](const ::testing::TestParamInfo<Case>& tp) { return std::string(tp.param.name); });

TEST(GeneratedCabinet, GroovedBackIsRecognised) {
    // drawerUnit: grooved back in grooves of both sides, the bottom and the top.
    const domain::Model m = generated(templates::drawerUnit(domain::AssemblyId("d1"), mats()));
    const ValidationReport r = validateModel(m);
    EXPECT_EQ(r.counts.errors, 0u) << toText(r);
    EXPECT_EQ(r.stats.grooveEngagementsAllowed, 4u) << toText(r);
}

TEST(GeneratedCabinet, GroovedBaseCabinetVariant) {
    CabinetDefinition d = templates::baseCabinet(domain::AssemblyId("b1"), mats());
    d.construction.back = furniture::BackMount::Grooved;
    const domain::Model m = generated(d);
    const ValidationReport r = validateModel(m);
    EXPECT_EQ(r.counts.errors, 0u) << toText(r);
    // Sides, bottom and (stretcher rails top) the rear rail carry the back.
    EXPECT_EQ(r.stats.grooveEngagementsAllowed, 4u) << toText(r);
}

TEST(GeneratedCabinet, ConfirmatsAreMatingPairsAndAnOffsetIsCaught) {
    domain::Model m = generated(templates::wallCabinet(domain::AssemblyId("w1"), mats()));
    const ValidationReport clean = validateModel(m);
    ASSERT_GT(clean.stats.matingHolePairs, 0u) << toText(clean);
    // Move the first hole of a confirmat rule on an EDGE face (the one in the end of the
    // mating panel) by 1.5 mm along u: its axis is now 1.5 mm away from its partner.
    std::set<std::string> confirmats; // sources of confirmat-generated holes = instance ids
    for (const auto& h : m.assemblies[0].hardware)
        if (h.definition.str() == hardware::ids::kConfirmat7x50) confirmats.insert(h.id.str());
    ASSERT_FALSE(confirmats.empty());
    bool moved = false;
    for (auto& p : m.assemblies[0].parts) {
        for (auto& op : p.operations) {
            auto* h = std::get_if<domain::Hole>(&op.shape);
            if (moved || !h || !confirmats.count(op.source)) continue;
            if (h->face == domain::Face::A || h->face == domain::Face::B) continue;
            h->u += 1.5;
            moved = true;
        }
    }
    ASSERT_TRUE(moved) << "no confirmat edge hole found";
    const ValidationReport r = validateModel(m);
    const auto list = findingsWithCode(r, "validation.machining.mating_holes_not_coaxial");
    ASSERT_EQ(list.size(), 1u) << toText(r);
    EXPECT_NE(list[0].message.find("axes are 1.5 mm apart"), std::string::npos) << list[0].message;
}

} // namespace
} // namespace fos::validation::test
