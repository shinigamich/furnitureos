// SPDX-License-Identifier: AGPL-3.0-only
#include <chrono>
#include <cstdio>

#include "test_support.hpp"

using namespace fos::parametric;
using namespace fos::parametric::test;
using ::testing::ElementsAre;

namespace {

std::string indexed(const char* stem, int i) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s%05d", stem, i);
    return buf;
}

double millisSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

constexpr int kCount = 10000;

} // namespace

TEST(Scale, ChainOfTenThousandLoadsOutOfOrderAndRecalculatesIncrementally) {
    // p00000 = 1, p(i) = p(i-1) + 1  =>  p(i) = i + 1. Records are supplied last-to-first.
    std::vector<ParameterDef> defs;
    for (int i = kCount - 1; i >= 1; --i) {
        defs.push_back(ParameterDef::formula(indexed("p", i), indexed("p", i - 1) + " + 1"));
    }
    defs.push_back(ParameterDef::constant(indexed("p", 0), 1));

    const auto t0 = std::chrono::steady_clock::now();
    auto loaded = ParameterGraph::load(defs);
    const double loadMs = millisSince(t0);
    FOS_ASSERT_OK(loaded);
    ParameterGraph g = std::move(loaded.value());
    EXPECT_EQ(g.size(), static_cast<std::size_t>(kCount));
    EXPECT_DOUBLE_EQ(valueOf(g, indexed("p", kCount - 1)), 10000.0);
    EXPECT_EQ(g.lastRecalculation().evaluated.size(), 9999u);

    // Root change: every expression is re-evaluated exactly once. 5 + 9999 = 10004.
    const auto t1 = std::chrono::steady_clock::now();
    FOS_ASSERT_OK(g.setValue(indexed("p", 0), 5));
    const double recalcMs = millisSince(t1);
    EXPECT_EQ(g.lastRecalculation().evaluated.size(), 9999u);
    EXPECT_DOUBLE_EQ(valueOf(g, indexed("p", kCount - 1)), 10004.0);

    // Mid-chain change: only the 3000 parameters downstream of p07000 (p07000 itself + 2999).
    auto r = g.setExpression(indexed("p", 7000), indexed("p", 6999) + " + 11"); // 7004 + 11 = 7015 (was 7005)
    FOS_ASSERT_OK(r);
    EXPECT_EQ(g.lastRecalculation().evaluated.size(), 3000u);
    EXPECT_EQ(g.lastRecalculation().evaluated.front(), indexed("p", 7000));
    EXPECT_EQ(g.lastRecalculation().evaluated.back(), indexed("p", kCount - 1));
    EXPECT_EQ(r.value().changed.size(), 3000u);
    EXPECT_DOUBLE_EQ(valueOf(g, indexed("p", 6999)), 7004.0);
    EXPECT_DOUBLE_EQ(valueOf(g, indexed("p", kCount - 1)), 10014.0);

    // The last link has no dependents: one evaluation.
    FOS_ASSERT_OK(g.setExpression(indexed("p", kCount - 1), indexed("p", kCount - 2) + " + 2"));
    EXPECT_THAT(g.lastRecalculation().evaluated, ElementsAre(indexed("p", kCount - 1)));

    // A cycle closing the whole chain is found (and reported) without recursion problems.
    auto cyc = g.setExpression(indexed("p", 0), indexed("p", kCount - 1) + " + 1");
    ASSERT_FALSE(cyc.ok());
    EXPECT_EQ(cyc.error().code, fos::core::ErrorCode::CyclicDependency);
    EXPECT_DOUBLE_EQ(valueOf(g, indexed("p", 0)), 5.0);

    std::printf("[ scale    ] chain: load %.1f ms, full recalculation %.1f ms\n", loadMs, recalcMs);
}

TEST(Scale, FanOfTenThousandReevaluatesOnlyDependentsOfTheChangedLeaf) {
    // 100 cabinets, each: gNNN.w (constant) and 99 parameters gNNN.pMM = gNNN.w + MM  => 10,000 parameters.
    Transaction tx;
    for (int group = 0; group < 100; ++group) {
        const std::string prefix = indexed("g", group);
        tx.define(ParameterDef::constant(prefix + ".w", 100));
        for (int j = 1; j <= 99; ++j) {
            tx.define(ParameterDef::formula(prefix + "." + indexed("p", j), prefix + ".w + " + std::to_string(j)));
        }
    }
    ParameterGraph g;
    const auto t0 = std::chrono::steady_clock::now();
    FOS_ASSERT_OK(g.apply(tx));
    const double loadMs = millisSince(t0);
    ASSERT_EQ(g.size(), static_cast<std::size_t>(kCount));
    EXPECT_EQ(g.lastRecalculation().evaluated.size(), 9900u);
    EXPECT_DOUBLE_EQ(valueOf(g, "g00042.p00007"), 107.0);

    // Change one leaf constant: exactly its 99 dependents are re-evaluated, nothing else.
    auto r = g.setValue("g00042.w", 200);
    FOS_ASSERT_OK(r);
    const RecalcStats& stats = g.lastRecalculation();
    ASSERT_EQ(stats.evaluated.size(), 99u);
    for (const auto& name : stats.evaluated) EXPECT_EQ(name.rfind("g00042.p", 0), 0u) << name;
    EXPECT_EQ(stats.visited, 100u);
    EXPECT_EQ(r.value().changed.size(), 100u);
    EXPECT_DOUBLE_EQ(valueOf(g, "g00042.p00007"), 207.0);
    EXPECT_DOUBLE_EQ(valueOf(g, "g00041.p00007"), 107.0);

    // Change one terminal expression: a single evaluation.
    FOS_ASSERT_OK(g.setExpression("g00007.p00099", "g00007.w * 2"));
    EXPECT_THAT(g.lastRecalculation().evaluated, ElementsAre("g00007.p00099"));
    EXPECT_DOUBLE_EQ(valueOf(g, "g00007.p00099"), 200.0);

    // Snapshot + change + restore on the big graph keeps both states intact.
    const Snapshot s = g.snapshot();
    FOS_ASSERT_OK(g.setValue("g00000.w", 1));
    EXPECT_DOUBLE_EQ(valueOf(g, "g00000.p00001"), 2.0);
    g.restore(s);
    EXPECT_DOUBLE_EQ(valueOf(g, "g00000.p00001"), 101.0);

    std::printf("[ scale    ] fan: load %.1f ms\n", loadMs);
}

TEST(Scale, WideFanFromOneParameter) {
    // base = 10 and 9,998 parameters leaf(i) = base + i, plus an unrelated pair.
    Transaction tx;
    tx.define(ParameterDef::constant("base", 10)).define(ParameterDef::constant("other", 1));
    tx.define(ParameterDef::formula("other_twice", "other * 2"));
    for (int i = 1; i <= kCount - 3; ++i) {
        tx.define(ParameterDef::formula(indexed("leaf", i), "base + " + std::to_string(i)));
    }
    ParameterGraph g;
    FOS_ASSERT_OK(g.apply(tx));
    ASSERT_EQ(g.size(), static_cast<std::size_t>(kCount));

    FOS_ASSERT_OK(g.setValue("other", 4));
    EXPECT_THAT(g.lastRecalculation().evaluated, ElementsAre("other_twice"));
    EXPECT_DOUBLE_EQ(valueOf(g, "other_twice"), 8.0);

    FOS_ASSERT_OK(g.setValue("base", 20));
    EXPECT_EQ(g.lastRecalculation().evaluated.size(), 9997u);
    EXPECT_DOUBLE_EQ(valueOf(g, indexed("leaf", 9997)), 10017.0); // 20 + 9997

    auto in_use = g.remove("base");
    ASSERT_FALSE(in_use.ok());
    EXPECT_THAT(in_use.error().message, ::testing::HasSubstr("and 9989 more")); // 9997 users, 8 listed
}
