// SPDX-License-Identifier: AGPL-3.0-only
#include "cli_test_support.hpp"

namespace fos::cli::test {
namespace {

TEST(CliLinear, CsvParseRunAndSvg) {
    TempDir dir;
    {
        std::ofstream f(dir.file("pieces.csv"), std::ios::binary);
        // Semicolon CSV, columns reordered, decimal comma. 1998 + 4 + 1998 + 4 + 1996 = 6000 (exact fit).
        f << "\xEF\xBB\xBFQuantity;Label;Length\r\n2;Rail;1998\r\n1;Post;1996,0\r\n";
    }
    auto r = fos({"linear", dir.file("pieces.csv"), "--stock", "6000", "--kerf", "4", "--svg-dir", dir.file("svg")});
    EXPECT_EQ(r.code, kExitOk) << r.out << r.err;
    EXPECT_TRUE(contains(r.out, "pieces: 3 required, 3 placed, 0 unplaced")) << r.out;
    EXPECT_TRUE(contains(r.out, "Rail 1998 @0 Rail 1998 @2002 Post 1996 @4004")) << r.out;
    EXPECT_TRUE(contains(r.out, "total: 1 bar(s), lower bound 1 (proven minimal)")) << r.out;
    EXPECT_TRUE(contains(r.out, "validator (linear::validateLinearPlan): no findings"));
    EXPECT_TRUE(contains(readFile(dir.path / "svg" / "linear-plan.svg"), "viewBox=\"0 0 6000 70\""));

    // Comma CSV with a piece longer than every stock: reported, exit 1.
    {
        std::ofstream f(dir.file("long.csv"), std::ios::binary);
        f << "label,length,quantity\nBeam,7000,1\n";
    }
    r = fos({"linear", dir.file("long.csv"), "--stock", "6000"});
    EXPECT_EQ(r.code, kExitValidationErrors);
    EXPECT_TRUE(contains(r.out, "UNPLACED: Piece 'Beam' (7000 mm) is longer than")) << r.out;
}

TEST(CliLinear, BadInputIsAUsageError) {
    TempDir dir;
    {
        std::ofstream f(dir.file("bad.csv"), std::ios::binary);
        f << "label;length;quantity\nA;abc;1\n";
    }
    auto r = fos({"linear", dir.file("bad.csv"), "--stock", "6000"});
    EXPECT_EQ(r.code, kExitUsageOrIo);
    EXPECT_TRUE(contains(r.err, "line 2: length 'abc'")) << r.err;
    EXPECT_EQ(fos({"linear", dir.file("bad.csv")}).code, kExitUsageOrIo); // --stock missing
    EXPECT_EQ(fos({"linear", dir.file("bad.csv"), "--stock", "-5"}).code, kExitUsageOrIo);
}

TEST(CliLinear, EdgebandRolls) {
    TempDir dir;
    const auto project = dir.file("kitchen.fosp");
    ASSERT_EQ(newBaseCabinet(project).code, kExitOk);
    auto r = fos({"edgeband-rolls", project});
    EXPECT_EQ(r.code, kExitOk) << r.out << r.err;
    EXPECT_TRUE(contains(r.out, "rolls needed: 1")) << r.out;
    EXPECT_TRUE(contains(r.out, "validator (linear::validateLinearPlan): no findings")) << r.out;
    EXPECT_EQ(fos({"edgeband-rolls", project, "--roll-length", "0"}).code, kExitUsageOrIo);
}

} // namespace
} // namespace fos::cli::test
