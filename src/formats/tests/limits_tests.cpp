// SPDX-License-Identifier: AGPL-3.0-only
// Hardening: bounded regular-file reads, the backup-count cap, listing-based backup rotation
// and bounded parsing of library text.
#include "fos/formats/atomic_file.hpp"
#include "fos/formats/library.hpp"
#include "test_support.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>

#ifndef _WIN32
#include <sys/stat.h>
#endif

using namespace fos::formats;
using fos::core::ErrorCode;
using fos::formats::test::TempDir;
using fos::formats::test::readText;
using fos::formats::test::writeText;
using ::testing::HasSubstr;
namespace fs = std::filesystem;

namespace {
FormatSpec spec() {
    FormatSpec s;
    s.kind = "furnitureos-project";
    return s;
}
Document revision(int n) {
    Document doc;
    doc.format = "furnitureos-project";
    doc.app.version = "0.1.0";
    doc.savedAt = "2026-01-31T12:00:00Z";
    doc.payload["revision"] = n;
    return doc;
}
int revisionOf(const fs::path& file) {
    auto loaded = loadDocument(file, spec());
    return loaded.ok() ? loaded.value().document.payload.value("revision", -1) : -1;
}
} // namespace

TEST(ReadFile, RefusesFifosAndDevicesWithoutBlocking) {
#ifdef _WIN32
    GTEST_SKIP() << "POSIX special files";
#else
    TempDir dir;
    const auto fifo = dir / "pipe.fosp";
    ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);
    // Opening a FIFO with a plain ifstream blocks until a writer appears; this must return.
    auto r = readFile(fifo);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::IoError);
    EXPECT_THAT(r.error().message, HasSubstr("is a FIFO (named pipe), not a regular file"));
    EXPECT_FALSE(loadDocument(fifo, spec()).ok());

    if (fs::exists("/dev/zero")) { // `fos info /dev/zero` used to read forever
        auto z = readFile("/dev/zero");
        ASSERT_FALSE(z.ok());
        EXPECT_THAT(z.error().message, HasSubstr("is a character device, not a regular file"));
    }
    auto d = readFile(dir.path());
    ASSERT_FALSE(d.ok());
    EXPECT_THAT(d.error().message, HasSubstr("is a directory"));
#endif
}

TEST(ReadFile, SizeLimitIsCheckedBeforeReading) {
    TempDir dir;
    // A sparse file of 300 MiB (no disk space used) exceeds the 256 MiB default.
    const auto big = dir / "big.fosp";
    { std::ofstream(big, std::ios::binary); }
    fs::resize_file(big, std::uintmax_t{300} * 1024 * 1024);
    const auto t0 = std::chrono::steady_clock::now();
    auto r = readFile(big);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::ConstraintViolation);
    // 300 * 1048576 = 314572800; 256 * 1048576 = 268435456.
    EXPECT_THAT(r.error().message, HasSubstr("is 314572800 bytes; the limit is 268435456 bytes"));
    EXPECT_LT(seconds, 1.0); // refused from the size, nothing was read
    EXPECT_EQ(kDefaultMaxFileBytes, 268435456u);

    // Configurable: exactly at the limit is read, one byte over is not.
    const auto small = dir / "small.txt";
    writeText(small, std::string(1000, 'x'));
    EXPECT_EQ(readFile(small, ReadLimits{1000}).value().size(), 1000u);
    auto over = readFile(small, ReadLimits{999});
    ASSERT_FALSE(over.ok());
    EXPECT_THAT(over.error().message, HasSubstr("is 1000 bytes; the limit is 999 bytes"));

    // loadDocument honours LoadOptions::maxFileBytes.
    const auto doc = dir / "doc.fosp";
    ASSERT_TRUE(saveDocument(doc, revision(1)).ok());
    LoadOptions tight;
    tight.maxFileBytes = fs::file_size(doc) - 1;
    auto refused = loadDocument(doc, spec(), tight);
    ASSERT_FALSE(refused.ok());
    EXPECT_EQ(refused.error().code, ErrorCode::ConstraintViolation);
    tight.maxFileBytes = fs::file_size(doc);
    EXPECT_TRUE(loadDocument(doc, spec(), tight).ok());
}

TEST(Backups, BackupCountIsCapped) {
    TempDir dir;
    const auto file = dir / "p.fosp";
    ASSERT_TRUE(saveDocument(file, revision(1)).ok());
    for (const int count : {101, 1000000, -1}) {
        SaveOptions o;
        o.backupCount = count;
        const auto t0 = std::chrono::steady_clock::now();
        auto r = saveDocument(file, revision(2), o);
        EXPECT_LT(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), 1.0);
        ASSERT_FALSE(r.ok()) << count;
        EXPECT_EQ(r.error().code, ErrorCode::InvalidArgument);
        EXPECT_THAT(r.error().message, HasSubstr("backupCount must be between 0 and 100, got " + std::to_string(count)));
    }
    EXPECT_EQ(revisionOf(file), 1); // untouched
    SaveOptions max;
    max.backupCount = kMaxBackupCount;
    ASSERT_TRUE(saveDocument(file, revision(3), max).ok());
    EXPECT_EQ(revisionOf(backupPath(file, 1)), 1);
}

TEST(Backups, RotationFromTheDirectoryListing) {
    TempDir dir;
    const auto file = dir / "p.fosp";
    // Files that look like backups but are not ours: other names, leading zero, beyond the count.
    writeText(dir / "p.fosp.bak007", "keep-a");
    writeText(dir / "p.fosp.bak4", "keep-b");
    writeText(dir / "p.fosp.bakx", "keep-c");
    writeText(dir / "q.fosp.bak1", "keep-d");
    SaveOptions o;
    o.backupCount = 3;
    for (int rev = 1; rev <= 6; ++rev) ASSERT_TRUE(saveDocument(file, revision(rev), o).ok()) << rev;
    // After saving 1..6: target 6, .bak1 = 5, .bak2 = 4, .bak3 = 3 (1 and 2 rotated out).
    EXPECT_EQ(revisionOf(file), 6);
    EXPECT_EQ(revisionOf(backupPath(file, 1)), 5);
    EXPECT_EQ(revisionOf(backupPath(file, 2)), 4);
    EXPECT_EQ(revisionOf(backupPath(file, 3)), 3);
    EXPECT_EQ(readText(dir / "p.fosp.bak4"), "keep-b");
    EXPECT_EQ(readText(dir / "p.fosp.bak007"), "keep-a");
    EXPECT_EQ(readText(dir / "p.fosp.bakx"), "keep-c");
    EXPECT_EQ(readText(dir / "q.fosp.bak1"), "keep-d");

    // A gap (only .bak1 and .bak3 exist): .bak3 is discarded, .bak1 -> .bak2, target -> .bak1.
    fs::remove(backupPath(file, 2));
    ASSERT_TRUE(saveDocument(file, revision(7), o).ok());
    EXPECT_EQ(revisionOf(file), 7);
    EXPECT_EQ(revisionOf(backupPath(file, 1)), 6);
    EXPECT_EQ(revisionOf(backupPath(file, 2)), 5);
    EXPECT_FALSE(fs::exists(backupPath(file, 3)));
    std::size_t entries = 0;
    for ([[maybe_unused]] const auto& e : fs::directory_iterator(dir.path())) ++entries;
    EXPECT_EQ(entries, 4u + 3u); // 4 foreign files + target + .bak1 + .bak2, no leftovers
}

TEST(LibraryText, ParsesTheSameAsTheFileAndIsBounded) {
    TempDir dir;
    Library lib;
    fos::domain::Material m;
    m.id = fos::domain::MaterialId("mat.a");
    m.name = "Board";
    m.thickness = 18;
    lib.materials.push_back(m);
    const auto file = dir / "a.foslib";
    SaveOptions o;
    o.backupCount = 0;
    ASSERT_TRUE(exportLibrary(file, lib, AppInfo{"FurnitureOS", "0.1.0"}, o).ok());
    const std::string text = readText(file);

    auto fromText = importLibraryText(text, "a.foslib");
    ASSERT_TRUE(fromText.ok()) << fromText.error().message;
    ASSERT_EQ(fromText.value().materials.size(), 1u);
    EXPECT_EQ(fromText.value().materials[0].id.str(), "mat.a");
    EXPECT_EQ(libraryToJson(fromText.value()), libraryToJson(importLibrary(file).value()));

    LoadOptions tight;
    tight.maxFileBytes = text.size() - 1;
    auto refused = importLibraryText(text, "a.foslib", tight);
    ASSERT_FALSE(refused.ok());
    EXPECT_EQ(refused.error().code, ErrorCode::ConstraintViolation);
    EXPECT_THAT(refused.error().message,
                HasSubstr("is " + std::to_string(text.size()) + " bytes; the limit is " + std::to_string(text.size() - 1)));
    EXPECT_FALSE(importLibrary(file, tight).ok()); // same limit for the file

    auto broken = importLibraryText("{", "b.foslib");
    ASSERT_FALSE(broken.ok());
    EXPECT_THAT(broken.error().message, HasSubstr("b.foslib"));
}
