// SPDX-License-Identifier: AGPL-3.0-only
// usableOffcuts(): free rectangles of a guillotine layout that are worth keeping as remnants.
#include "fos/cutting/offcuts.hpp"
#include "test_support.hpp"

#include <cmath>
#include <limits>

using namespace fos::cutting;
using namespace fos::cutting::test;

namespace {

void expectRect(const Rect& r, double x, double y, double length, double width) {
    EXPECT_NEAR(r.x, x, 1e-9);
    EXPECT_NEAR(r.y, y, 1e-9);
    EXPECT_NEAR(r.length, length, 1e-9);
    EXPECT_NEAR(r.width, width, 1e-9);
}

} // namespace

// One piece 1500 x 800 (no rotation) on a 2000 x 1000 sheet, kerf 4, no trim. The piece sits at
// the origin; beyond the kerf 2000 - 1500 - 4 = 496 mm remain along X and 1000 - 800 - 4 = 196
// along Y. The kept split is crosswise first (see stock_tests): right strip at x = 1504,
// 496 x 1000 (496000 mm2), top strip at y = 804, 1500 x 196 (294000 mm2).
TEST(UsableOffcuts, FreeRectanglesOfTheLayoutHandChecked) {
    Job job = makeJob(2000, 1000, 4);
    addPiece(job, "p", 1500, 800, 1, false);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    ASSERT_EQ(r.sheets.size(), 1u);

    auto o = usableOffcuts(r.sheets[0], 300, 100);
    ASSERT_EQ(o.size(), 2u);
    expectRect(o[0].rect, 1504, 0, 496, 1000); // larger area first
    expectRect(o[1].rect, 0, 804, 1500, 196);
    EXPECT_EQ(o[0].sheetIndex, -1);

    // 300 x 200: the 196 mm strip is too narrow.
    o = usableOffcuts(r.sheets[0], 300, 200);
    ASSERT_EQ(o.size(), 1u);
    expectRect(o[0].rect, 1504, 0, 496, 1000);

    // 1100 x 100: the right strip's longer side is 1000 < 1100; the top strip (1500 >= 1100,
    // 196 >= 100) stays. The limits apply to the longer/shorter side, not to X/Y.
    o = usableOffcuts(r.sheets[0], 1100, 100);
    ASSERT_EQ(o.size(), 1u);
    expectRect(o[0].rect, 0, 804, 1500, 196);

    // Exactly at the limit counts as usable (196 >= 196).
    EXPECT_EQ(usableOffcuts(r.sheets[0], 1500, 196).size(), 1u);
    EXPECT_TRUE(usableOffcuts(r.sheets[0], 1500.5, 196).empty());
}

// Same piece with 10 mm trim on every side: usable area [10, 1990] x [10, 990] = 1980 x 980; the
// piece at (10, 10). Beyond it 1990 - 1510 - 4 = 476 along X, 990 - 810 - 4 = 176 along Y.
// Crosswise first: 476 x 980 + 1500 x 176 = 466480 + 264000 = 730480; lengthwise first:
// 1980 x 176 + 476 x 800 = 348480 + 380800 = 729280 -> crosswise first is kept.
TEST(UsableOffcuts, TrimIsNotPartOfAnOffcut) {
    Job job = makeJob(2000, 1000, 4);
    job.trim = {10, 10, 10, 10};
    addPiece(job, "p", 1500, 800, 1, false);
    const CuttingResult r = solve(job);
    expectValid(job, r);
    const auto o = usableOffcuts(r, 300, 100);
    ASSERT_EQ(o.size(), 2u);
    EXPECT_EQ(o[0].sheetIndex, 0);
    expectRect(o[0].rect, 1514, 10, 476, 980);
    expectRect(o[1].rect, 10, 814, 1500, 176);
}

TEST(UsableOffcuts, OrderTiesAndDegenerateInput) {
    SheetLayout s;
    s.offcuts = {{{0, 500, 400, 200}, true},  // 80000, y 500
                 {{600, 0, 200, 400}, true},  // 80000, y 0  -> before the one above
                 {{0, 0, 50, 50}, false},     // too small
                 {{900, 0, 0, 900}, false},   // zero width: never an offcut
                 {{0, 900, 1000, 300}, true}}; // 300000 -> first
    const auto o = usableOffcuts(s, 300, 100);
    ASSERT_EQ(o.size(), 3u);
    EXPECT_EQ(o[0].offcutIndex, 4);
    EXPECT_EQ(o[1].offcutIndex, 1);
    EXPECT_EQ(o[2].offcutIndex, 0);
    // Negative limits behave as 0 (every non-empty offcut); NaN limits give nothing.
    EXPECT_EQ(usableOffcuts(s, -5, -5).size(), 4u);
    EXPECT_TRUE(usableOffcuts(s, std::numeric_limits<double>::quiet_NaN(), 100).empty());
    EXPECT_TRUE(usableOffcuts(SheetLayout{}, 0, 0).empty());
}
