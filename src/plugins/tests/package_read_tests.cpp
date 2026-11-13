// SPDX-License-Identifier: AGPL-3.0-only
// Libraries are read ONCE through the bounded reader (no size-check / re-read TOCTOU window),
// and special files (FIFOs) are refused without blocking.
#include "fos/plugins/package.hpp"
#include "plugin_test_support.hpp"

#include <gmock/gmock.h>

#ifndef _WIN32
#include <sys/stat.h>
#endif

using namespace fos;
using namespace fos::plugins;
using namespace fos::plugins::test;
using ::testing::HasSubstr;

TEST(PackageRead, LibraryThatGrowsAfterInspectionIsRefusedWithoutAnUnboundedRead) {
    TempDir tmp;
    const auto dir = makePlugin(tmp.path(), "org.example.grow");
    const auto libSize = fs::file_size(dir / "lib/main.foslib");
    PluginLimits limits;
    limits.maxLibraryBytes = libSize + 100; // the inspected file fits
    auto inspection = inspectPackage(dir, limits);
    ASSERT_TRUE(inspection.ok()) << allMessages(inspection.diagnostics);

    // The file is replaced after inspection by one 1000 bytes too large (valid JSON
    // whitespace appended, so only the size can make the load fail).
    const std::string original = readText(dir / "lib/main.foslib");
    writeText(dir / "lib/main.foslib", original + std::string(1100, ' '));
    const auto grownSize = libSize + 1100;
    auto lib = loadPackageLibraries(*inspection.package, limits);
    ASSERT_FALSE(lib.ok());
    EXPECT_EQ(lib.error().code, core::ErrorCode::ConstraintViolation);
    EXPECT_THAT(lib.error().message, HasSubstr("is " + std::to_string(grownSize) + " bytes; the limit is " +
                                               std::to_string(libSize + 100) + " bytes"));
    EXPECT_THAT(lib.error().message, HasSubstr("org.example.grow"));

    // At exactly the limit the same padded file loads (trailing whitespace is valid JSON).
    limits.maxLibraryBytes = grownSize;
    auto ok = loadPackageLibraries(*inspection.package, limits);
    ASSERT_TRUE(ok.ok()) << ok.error().message;
    EXPECT_EQ(ok.value().materials.size(), 1u);
}

TEST(PackageRead, ReadBoundedRefusesFifosAndDirectoriesWithoutBlocking) {
#ifdef _WIN32
    GTEST_SKIP() << "no FIFOs";
#else
    TempDir tmp;
    const auto fifo = tmp / "pipe.foslib";
    ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);
    auto r = readBounded(fifo, 1024); // an ifstream would block here until a writer appears
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::IoError);
    EXPECT_THAT(r.error().message, HasSubstr("FIFO"));
    auto d = readBounded(tmp.path(), 1024);
    ASSERT_FALSE(d.ok());
    EXPECT_THAT(d.error().message, HasSubstr("is a directory"));

    // A library swapped for a FIFO after inspection is refused at load time.
    const auto dir = makePlugin(tmp.path(), "org.example.fifo");
    auto inspection = inspectPackage(dir);
    ASSERT_TRUE(inspection.ok()) << allMessages(inspection.diagnostics);
    fs::remove(dir / "lib/main.foslib");
    ASSERT_EQ(::mkfifo((dir / "lib/main.foslib").c_str(), 0600), 0);
    auto lib = loadPackageLibraries(*inspection.package);
    ASSERT_FALSE(lib.ok());
    EXPECT_THAT(lib.error().message, HasSubstr("FIFO"));
#endif
}
