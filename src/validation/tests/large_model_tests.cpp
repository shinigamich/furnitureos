// SPDX-License-Identifier: AGPL-3.0-only
/// 1,000-part synthetic model: correctness and broad-phase effectiveness.
#include "test_support.hpp"

namespace fos::validation::test {
namespace {

TEST(LargeModel, ThousandPartsSweepAndPrune) {
    // 10 x 10 x 10 grid of 50 x 40 x 18 boards at (60 i, 50 j, 30 k): gaps of 10 mm in X,
    // 10 in Y and 12 in Z. Board (0,0,0) is lifted to z = 20, into board (0,0,1) at z 30..48:
    // separating translations X 50, Y 40, Z 38 - 30 = 8 -> 8 mm along Z (both 18 mm along Z,
    // so the first of the pair is named).
    std::vector<domain::Part> parts;
    for (int i = 0; i < 10; ++i)
        for (int j = 0; j < 10; ++j)
            for (int k = 0; k < 10; ++k) {
                const std::string id = std::to_string(i) + "." + std::to_string(j) + "." + std::to_string(k);
                domain::Part p = board(id, 50, 40, geometry::Transform::translation({60.0 * i, 50.0 * j, 30.0 * k}));
                p.name = "P " + id;
                parts.push_back(p);
            }
    parts[0].placement.origin.z = 20;
    const domain::Model m = libraryModel(parts);
    const ValidationReport r = validateModel(m);

    EXPECT_EQ(r.counts.errors, 1u) << toText(r);
    EXPECT_EQ(r.counts.warnings, 0u) << toText(r);
    EXPECT_EQ(only(r, "validation.interference.parts").message, "'P 0.0.0' penetrates 'P 0.0.1' by 8 mm along Z.");

    EXPECT_EQ(r.stats.parts, 1000u);
    EXPECT_EQ(r.stats.possiblePartPairs, 499500u); // 1000 * 999 / 2
    // The sweep along X only pairs boards of the same X column (columns 60 apart, 50 wide):
    // 10 columns * (100 * 99 / 2) = 49,500 box comparisons ...
    EXPECT_EQ(r.stats.broadPhaseChecks, 49500u);
    // ... of which only the lifted pair has overlapping boxes -> one exact test.
    EXPECT_EQ(r.stats.partPairsTested, 1u);
    EXPECT_LT(r.stats.partPairsTested * 1000, r.stats.possiblePartPairs);
}

} // namespace
} // namespace fos::validation::test
