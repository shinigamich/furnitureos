// SPDX-License-Identifier: AGPL-3.0-only
// Routing split (ADR-0008 decision 5): rectangular parts -> guillotine cutting (unchanged),
// shaped parts -> true-shape nesting. Every part goes to exactly one of the two.
#include "phase12_model.hpp"
#include "test_support.hpp"

#include "fos/cutting/from_model.hpp"
#include "fos/cutting/optimizer.hpp"
#include "fos/cutting/validate.hpp"
#include "fos/nesting/from_model.hpp"

#include <set>

using namespace fos::nesting::test;
namespace domain = fos::domain;
namespace cutting = fos::cutting;
namespace nesting = fos::nesting;

TEST(RoutingSplit, RectanglesUnchangedShapedPartsNested) {
    const domain::Model base = domain::fixtures::basicCabinet();
    const domain::Model model = phase12Model();

    fos::core::Diagnostics baseDs, ds;
    const cutting::Settings cs;
    const auto before = cutting::jobsFromModel(base, cs, baseDs);
    const cutting::ModelCutting split = cutting::cuttingFromModel(model, cs, ds);

    // Guillotine jobs identical to the model without the shaped parts.
    ASSERT_EQ(split.jobs.size(), before.size());
    for (std::size_t j = 0; j < before.size(); ++j) {
        ASSERT_EQ(split.jobs[j].pieces.size(), before[j].pieces.size());
        EXPECT_EQ(split.jobs[j].id, before[j].id);
        for (std::size_t i = 0; i < before[j].pieces.size(); ++i) {
            const auto& a = split.jobs[j].pieces[i];
            const auto& b = before[j].pieces[i];
            EXPECT_EQ(a.id, b.id);
            EXPECT_EQ(a.label, b.label);
            EXPECT_EQ(a.length, b.length);
            EXPECT_EQ(a.width, b.width);
            EXPECT_EQ(a.quantity, b.quantity);
            EXPECT_EQ(a.canRotate, b.canRotate);
            EXPECT_EQ(a.sourceIds, b.sourceIds);
        }
    }
    // jobsFromModel applies the same split.
    fos::core::Diagnostics ds2;
    EXPECT_EQ(cutting::jobsFromModel(model, cs, ds2).size(), split.jobs.size());

    // The three shaped parts are reported, each with one Info diagnostic.
    ASSERT_EQ(split.routedParts.size(), 3u);
    EXPECT_EQ(split.routedParts[0].partId, "shaped/rounded");
    EXPECT_EQ(split.routedParts[1].partId, "shaped/arched");
    EXPECT_EQ(split.routedParts[2].partId, "shaped/worktop");
    EXPECT_EQ(split.routedParts[2].label, "Worktop");
    EXPECT_EQ(split.routedParts[2].materialId, "mat.pb18");
    ASSERT_EQ(ds.size(), 3u);
    for (const auto& d : ds) {
        EXPECT_EQ(d.severity, fos::core::Severity::Info);
        EXPECT_EQ(d.code, "cutting.adapter.routed");
    }

    // Guillotine jobs still optimise and validate.
    for (const auto& job : split.jobs) {
        auto r = cutting::optimize(job);
        ASSERT_TRUE(r.ok());
        EXPECT_TRUE(cutting::validateLayout(job, r.value()).empty());
    }

    // Nesting jobs: exactly the routed parts; disjoint from the guillotine pieces.
    fos::core::Diagnostics nds;
    const auto nj = nesting::nestingJobsFromModel(model, nesting::ModelSettings{}, nds);
    EXPECT_TRUE(nds.empty()) << codes(nds);
    std::set<std::string> nested, sawn;
    for (const auto& m : nj)
        for (const auto& p : m.job.parts) nested.insert(p.id);
    for (const auto& j : split.jobs)
        for (const auto& p : j.pieces) sawn.insert(p.sourceIds.begin(), p.sourceIds.end());
    std::set<std::string> routed;
    for (const auto& r : split.routedParts) routed.insert(r.partId);
    EXPECT_EQ(nested, routed);
    for (const auto& id : nested) EXPECT_EQ(sawn.count(id), 0u) << id;
    std::size_t total = 0;
    for (const auto& a : model.assemblies) total += a.parts.size();
    EXPECT_EQ(nested.size() + sawn.size(), total);
    for (const auto& m : nj) nestValid(m.job);
}
