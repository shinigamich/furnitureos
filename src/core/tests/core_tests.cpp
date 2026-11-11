// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/core/diagnostic.hpp"
#include "fos/core/id.hpp"
#include "fos/core/log.hpp"
#include "fos/core/result.hpp"
#include "fos/core/units.hpp"

#include <gtest/gtest.h>
#include <set>

using namespace fos::core;

TEST(Units, Conversions) {
    EXPECT_DOUBLE_EQ(inchesToMm(1.0), 25.4);
    EXPECT_DOUBLE_EQ(mmToInches(50.8), 2.0);
    EXPECT_DOUBLE_EQ(cmToMm(1.5), 15.0);
    EXPECT_DOUBLE_EQ(mToMm(2.8), 2800.0);
    EXPECT_NEAR(degToRad(180.0), kPi, 1e-15);
}

TEST(Units, ApproxComparisons) {
    EXPECT_TRUE(approxEqual(764.0, 764.0 + 1e-9));
    EXPECT_FALSE(approxEqual(764.0, 764.001));
    EXPECT_FALSE(definitelyLess(1.0, 1.0 + 1e-9));
    EXPECT_TRUE(definitelyLess(1.0, 1.1));
    EXPECT_TRUE(definitelyGreater(1.1, 1.0));
}

TEST(Units, FormatLength) {
    EXPECT_EQ(formatLength(764.0), "764");
    EXPECT_EQ(formatLength(719.2), "719.2");
    EXPECT_EQ(formatLength(719.25), "719.3");
    EXPECT_EQ(formatLength(-0.01), "0");
    EXPECT_EQ(formatLength(0.4, 2), "0.4");
    EXPECT_EQ(formatLength(1234.5678, 3), "1234.568");
}

TEST(Id, UuidShapeAndUniqueness) {
    std::set<std::string> seen;
    for (int i = 0; i < 1000; ++i) {
        std::string u = newUuid();
        ASSERT_EQ(u.size(), 36u);
        EXPECT_EQ(u[14], '4');
        EXPECT_TRUE(u[19] == '8' || u[19] == '9' || u[19] == 'a' || u[19] == 'b');
        EXPECT_TRUE(seen.insert(u).second);
    }
}

TEST(Id, TypedIdsCompare) {
    struct TagA;
    TypedId<TagA> a("x"), b("x"), c("y");
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_LT(a, c);
    EXPECT_TRUE(TypedId<TagA>().empty());
}

TEST(Result, ValueAndError) {
    Result<int> ok(5);
    ASSERT_TRUE(ok.ok());
    EXPECT_EQ(ok.value(), 5);
    Result<int> bad(makeError(ErrorCode::NotFound, "Material 'x' does not exist.", "x"));
    ASSERT_FALSE(bad.ok());
    EXPECT_EQ(bad.error().code, ErrorCode::NotFound);
    EXPECT_EQ(bad.valueOr(7), 7);
    Result<void> v;
    EXPECT_TRUE(v.ok());
    Result<void> e(makeError(ErrorCode::IoError, "disk full"));
    EXPECT_FALSE(e.ok());
    EXPECT_STREQ(toString(e.error().code), "IoError");
}

TEST(Log, SinkReceivesMessages) {
    int count = 0;
    setLogSink([&](LogLevel, std::string_view, std::string_view) { ++count; });
    setLogLevel(LogLevel::Warning);
    logInfo("test", "ignored");
    logError("test", "seen");
    setLogSink(nullptr);
    setLogLevel(LogLevel::Info);
    EXPECT_EQ(count, 1);
}

TEST(Diagnostics, HasErrors) {
    Diagnostics ds{{Severity::Warning, "w", "warn", ""}};
    EXPECT_FALSE(hasErrors(ds));
    ds.push_back({Severity::Error, "e", "err", ""});
    EXPECT_TRUE(hasErrors(ds));
}
