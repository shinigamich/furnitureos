// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/plugins/semver.hpp"

#include <gtest/gtest.h>

using namespace fos::plugins;

namespace {
SemVer v(const char* text) {
    auto r = parseSemVer(text);
    EXPECT_TRUE(r.ok()) << text << ": " << (r.ok() ? "" : r.error().message);
    return r.ok() ? r.value() : SemVer{};
}
} // namespace

TEST(SemVer, ParsesAllParts) {
    const auto s = v("12.0.345-rc.7.x-y+build.2024");
    EXPECT_EQ(s.major, 12u);
    EXPECT_EQ(s.minor, 0u);
    EXPECT_EQ(s.patch, 345u);
    ASSERT_EQ(s.prerelease.size(), 3u);
    EXPECT_FALSE(s.prerelease[0].numeric);
    EXPECT_TRUE(s.prerelease[1].numeric);
    EXPECT_EQ(s.prerelease[1].number, 7u);
    EXPECT_EQ(s.prerelease[2].text, "x-y");
    ASSERT_EQ(s.build.size(), 2u);
    EXPECT_EQ(s.toString(), "12.0.345-rc.7.x-y+build.2024");
    EXPECT_EQ(v("18446744073709551615.0.0").major, 18446744073709551615ull); // 2^64 - 1
    EXPECT_EQ(v("1.0.0+001").build[0], "001"); // leading zeros allowed in build metadata
    EXPECT_EQ(v("1.0.0-0a").prerelease[0].numeric, false); // alphanumeric, leading zero fine
}

TEST(SemVer, RejectsInvalidText) {
    for (const char* bad : {"", "1", "1.0", "1.0.0.0", "v1.0.0", " 1.0.0", "1.0.0 ", "01.0.0", "1.02.0", "1.0.00",
                            "1.0.0-", "1.0.0-a..b", "1.0.0-01", "1.0.0+", "1.0.0+a+b", "1.0.0-a_b", "-1.0.0", "1.a.0",
                            "18446744073709551616.0.0", "1.0.0-99999999999999999999", "1..0"}) {
        auto r = parseSemVer(bad);
        EXPECT_FALSE(r.ok()) << "accepted \"" << bad << "\"";
        if (!r.ok()) {
            EXPECT_NE(r.error().message.find("not a valid semantic version"), std::string::npos);
        }
    }
}

TEST(SemVer, PrecedenceTableFromSpecification) {
    // semver.org section 11, strictly increasing.
    const char* ordered[] = {"1.0.0-alpha",  "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2",
                             "1.0.0-beta.11", "1.0.0-rc.1",   "1.0.0",            "1.0.1",      "1.1.0",
                             "1.10.0",        "2.0.0-0",      "2.0.0",            "2.1.0",      "2.1.1",
                             "10.0.0"};
    const std::size_t n = std::size(ordered);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j) {
            const auto a = v(ordered[i]);
            const auto b = v(ordered[j]);
            EXPECT_EQ(compare(a, b) < 0, i < j) << ordered[i] << " vs " << ordered[j];
            EXPECT_EQ(compare(a, b) == 0, i == j) << ordered[i] << " vs " << ordered[j];
        }
}

TEST(SemVer, NumericIdentifiersCompareNumericallyAndBuildIsIgnored) {
    EXPECT_LT(v("1.0.0-beta.2"), v("1.0.0-beta.11")); // 2 < 11 numerically (not "11" < "2")
    EXPECT_LT(v("1.0.0-9"), v("1.0.0-a"));            // numeric < alphanumeric
    EXPECT_LT(v("1.0.0-B"), v("1.0.0-a"));            // ASCII: 'B' (66) < 'a' (97)
    EXPECT_TRUE(samePrecedence(v("1.0.0+a"), v("1.0.0+b")));
    EXPECT_TRUE(samePrecedence(v("1.0.0-rc.1+x"), v("1.0.0-rc.1")));
    EXPECT_GT(v("0.2.0"), v("0.2.0-dev"));
}
