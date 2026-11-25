// SPDX-License-Identifier: AGPL-3.0-only
#include "cli_test_support.hpp"

#include "fos/formats/json.hpp"

#include <filesystem>

namespace fos::cli::test {
namespace {

TEST(CliDiff, ExitCodesTextAndJson) {
    TempDir dir;
    const auto a = dir.file("a.fosp"), b = dir.file("b.fosp");
    ASSERT_EQ(newBaseCabinet(a).code, kExitOk);
    std::filesystem::copy_file(u8path(a), u8path(b));

    auto r = fos({"diff", a, b});
    EXPECT_EQ(r.code, kExitOk) << r.out << r.err;
    EXPECT_EQ(r.out, "No differences.\n");

    // newBaseCabinet makes cab1 800 wide; set it to 900 in B.
    ASSERT_EQ(fos({"set", b, "cab1", "width", "900"}).code, kExitOk);
    r = fos({"diff", a, b, "--ignore-timestamps", "--model"});
    EXPECT_EQ(r.code, kExitValidationErrors) << r.out << r.err;
    EXPECT_TRUE(contains(r.out, "parameter width: 800 -> 900")) << r.out;
    // Bottom between the sides: 800 - 2 * 18 = 764 -> 900 - 36 = 864.
    EXPECT_TRUE(contains(r.out, "~ part cab1/bottom")) << r.out;
    EXPECT_TRUE(contains(r.out, "764 x")) << r.out;
    EXPECT_TRUE(contains(r.out, "864 x")) << r.out;

    r = fos({"diff", a, b, "--format", "json"});
    EXPECT_EQ(r.code, kExitValidationErrors);
    const auto j = formats::Json::parse(r.out, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << r.out;
    EXPECT_FALSE(j["identical"].get<bool>());
    EXPECT_EQ(j["items"]["changed"][0]["parameters"][0]["name"], "width");
    EXPECT_FALSE(j.contains("model"));

    EXPECT_EQ(fos({"diff", a}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"diff", a, b, "--format", "xml"}).code, kExitUsageOrIo);
    EXPECT_EQ(fos({"diff", a, dir.file("missing.fosp")}).code, kExitUsageOrIo);
}

} // namespace
} // namespace fos::cli::test
