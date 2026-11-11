// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/formats/atomic_file.hpp"
#include "fos/formats/recovery.hpp"
#include "test_support.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <map>

#ifndef _WIN32
#include <unistd.h>
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
    s.currentVersion = 1;
    return s;
}

/// Document whose payload is {"revision": n}.
Document revision(int n) {
    Document doc;
    doc.format = "furnitureos-project";
    doc.app.version = "0.1.0";
    doc.savedAt = "2026-01-31T12:00:00Z";
    doc.payload["revision"] = n;
    return doc;
}

/// Revision stored in a file, or -1 when it does not load.
int revisionOf(const fs::path& file) {
    auto loaded = loadDocument(file, spec());
    return loaded.ok() ? loaded.value().document.payload.value("revision", -1) : -1;
}

/// File name -> content of every entry of a directory.
std::map<std::string, std::string> snapshot(const fs::path& directory) {
    std::map<std::string, std::string> out;
    for (const auto& entry : fs::directory_iterator(directory))
        out[entry.path().filename().string()] = readText(entry.path());
    return out;
}

SaveOptions failingAt(SaveStage stage) {
    SaveOptions options;
    options.beforeStage = [stage](SaveStage current) -> fos::core::Result<void> {
        if (current == stage) return fos::core::makeError(ErrorCode::IoError, "Injected failure.");
        return {};
    };
    return options;
}

} // namespace

TEST(AtomicSave, CreatesFileThatLoadsAndReportsChecksum) {
    TempDir dir;
    const fs::path file = dir / "kitchen.fosp";
    Document doc = revision(1);
    doc.savedAt.clear();
    SaveOptions options;
    options.clock = [] { return std::chrono::system_clock::time_point{std::chrono::seconds{1700000000}}; };
    auto report = saveDocument(file, doc, options);
    ASSERT_TRUE(report.ok()) << report.error().message;
    EXPECT_EQ(report.value().path, file);
    EXPECT_EQ(report.value().savedAt, "2023-11-14T22:13:20Z"); // from the injected clock
    EXPECT_EQ(report.value().checksum, payloadChecksum(doc.payload).value());
    EXPECT_EQ(report.value().bytesWritten, fs::file_size(file));

    auto loaded = loadDocument(file, spec());
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    EXPECT_EQ(loaded.value().document.savedAt, "2023-11-14T22:13:20Z");
    EXPECT_EQ(loaded.value().document.payload["revision"], 1);
    // Only the target exists: no temporary file, no backup for a first save.
    EXPECT_EQ(snapshot(dir.path()).size(), 1u);
}

TEST(AtomicSave, ExplicitSavedAtWinsOverTheClock) {
    TempDir dir;
    SaveOptions options;
    options.clock = [] { return std::chrono::system_clock::time_point{}; };
    auto report = saveDocument(dir / "a.fosp", revision(1), options);
    ASSERT_TRUE(report.ok());
    EXPECT_EQ(report.value().savedAt, "2026-01-31T12:00:00Z");
}

TEST(AtomicSave, RotatesBackupsUpToTheConfiguredCount) {
    TempDir dir;
    const fs::path file = dir / "kitchen.fosp";
    SaveOptions options;
    options.backupCount = 2;
    for (int n = 1; n <= 4; ++n) ASSERT_TRUE(saveDocument(file, revision(n), options).ok()) << n;
    // After revisions 1..4 with two backups: target = 4, .bak1 = 3, .bak2 = 2, revision 1 dropped.
    EXPECT_EQ(revisionOf(file), 4);
    EXPECT_EQ(revisionOf(backupPath(file, 1)), 3);
    EXPECT_EQ(revisionOf(backupPath(file, 2)), 2);
    EXPECT_FALSE(fs::exists(backupPath(file, 3)));
    EXPECT_EQ(snapshot(dir.path()).size(), 3u); // no temporary or parked files left behind
    EXPECT_EQ(backupPath(file, 1).filename().string(), "kitchen.fosp.bak1");
}

TEST(AtomicSave, FirstBackupsAppearOneByOne) {
    TempDir dir;
    const fs::path file = dir / "k.fosp";
    ASSERT_TRUE(saveDocument(file, revision(1)).ok()); // default: 3 backups
    EXPECT_FALSE(fs::exists(backupPath(file, 1)));
    ASSERT_TRUE(saveDocument(file, revision(2)).ok());
    EXPECT_EQ(revisionOf(backupPath(file, 1)), 1);
    EXPECT_FALSE(fs::exists(backupPath(file, 2)));
    ASSERT_TRUE(saveDocument(file, revision(3)).ok());
    EXPECT_EQ(revisionOf(backupPath(file, 1)), 2);
    EXPECT_EQ(revisionOf(backupPath(file, 2)), 1);
}

TEST(AtomicSave, BackupCountZeroMakesNoBackups) {
    TempDir dir;
    const fs::path file = dir / "k.fosp";
    SaveOptions options;
    options.backupCount = 0;
    ASSERT_TRUE(saveDocument(file, revision(1), options).ok());
    ASSERT_TRUE(saveDocument(file, revision(2), options).ok());
    EXPECT_EQ(revisionOf(file), 2);
    EXPECT_EQ(snapshot(dir.path()).size(), 1u);
    options.backupCount = -1;
    EXPECT_EQ(saveDocument(file, revision(3), options).error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(revisionOf(file), 2);
}

TEST(AtomicSave, FailureAtAnyStageLeavesFileAndBackupsIntact) {
    for (const SaveStage stage :
         {SaveStage::WriteTemp, SaveStage::Verify, SaveStage::RotateBackups, SaveStage::Replace}) {
        TempDir dir;
        const fs::path file = dir / "kitchen.fosp";
        SaveOptions good;
        good.backupCount = 2;
        for (int n = 1; n <= 3; ++n) ASSERT_TRUE(saveDocument(file, revision(n), good).ok());
        const auto before = snapshot(dir.path()); // target = 3, bak1 = 2, bak2 = 1
        ASSERT_EQ(before.size(), 3u);

        SaveOptions failing = failingAt(stage);
        failing.backupCount = 2;
        auto r = saveDocument(file, revision(4), failing);
        ASSERT_FALSE(r.ok()) << toString(stage);
        EXPECT_EQ(r.error().code, ErrorCode::IoError);
        EXPECT_THAT(r.error().message, HasSubstr(std::string("(") + toString(stage) + " stage)"));
        EXPECT_THAT(r.error().message, HasSubstr("the existing file was left unchanged"));
        EXPECT_THAT(r.error().message, HasSubstr("Injected failure."));
        // Byte-identical directory: same files, same contents, no leftovers.
        EXPECT_EQ(snapshot(dir.path()), before) << toString(stage);
        EXPECT_EQ(revisionOf(file), 3);
        EXPECT_EQ(revisionOf(backupPath(file, 1)), 2);
        EXPECT_EQ(revisionOf(backupPath(file, 2)), 1);

        // And the next regular save works normally.
        ASSERT_TRUE(saveDocument(file, revision(5), good).ok());
        EXPECT_EQ(revisionOf(file), 5);
        EXPECT_EQ(revisionOf(backupPath(file, 1)), 3);
        EXPECT_EQ(revisionOf(backupPath(file, 2)), 2);
    }
}

TEST(AtomicSave, FailingWriterLeavesOldFileIntact) {
    TempDir dir;
    const fs::path file = dir / "k.fosp";
    ASSERT_TRUE(saveDocument(file, revision(1)).ok());
    const auto before = snapshot(dir.path());

    SaveOptions options;
    options.writer = [](const fs::path& temp, std::string_view bytes) -> fos::core::Result<void> {
        writeText(temp, std::string(bytes.substr(0, bytes.size() / 2))); // half written, then "disk full"
        return fos::core::makeError(ErrorCode::IoError, "No space left on device.");
    };
    auto r = saveDocument(file, revision(2), options);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::IoError);
    EXPECT_THAT(r.error().message, HasSubstr("No space left on device."));
    EXPECT_EQ(snapshot(dir.path()), before);
}

TEST(AtomicSave, SilentlyTruncatedOrCorruptedWriteIsCaughtByVerification) {
    TempDir dir;
    const fs::path file = dir / "k.fosp";
    ASSERT_TRUE(saveDocument(file, revision(1)).ok());
    const auto before = snapshot(dir.path());

    SaveOptions truncating; // reports success but drops the tail
    truncating.writer = [](const fs::path& temp, std::string_view bytes) -> fos::core::Result<void> {
        writeText(temp, std::string(bytes.substr(0, bytes.size() - 10)));
        return {};
    };
    auto r = saveDocument(file, revision(2), truncating);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::CorruptData);
    EXPECT_THAT(r.error().message, HasSubstr("(Verify stage)"));
    EXPECT_EQ(snapshot(dir.path()), before);

    SaveOptions flipping; // same length, one digit changed inside the payload
    flipping.writer = [](const fs::path& temp, std::string_view bytes) -> fos::core::Result<void> {
        std::string copy(bytes);
        copy[copy.find("\"revision\": 2") + 12] = '7';
        writeText(temp, copy);
        return {};
    };
    EXPECT_EQ(saveDocument(file, revision(2), flipping).error().code, ErrorCode::CorruptData);
    EXPECT_EQ(snapshot(dir.path()), before);
    EXPECT_EQ(revisionOf(file), 1);
}

TEST(AtomicSave, VerifierOfAtomicWriteFileSeesTheBytesOnDisk) {
    TempDir dir;
    const fs::path file = dir / "notes.txt";
    ASSERT_TRUE(atomicWriteFile(file, "old").ok());
    std::string seen;
    auto r = atomicWriteFile(file, "new content", {}, [&](std::string_view bytes) -> fos::core::Result<void> {
        seen = std::string(bytes);
        return fos::core::makeError(ErrorCode::CorruptData, "Rejected by the verifier.");
    });
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(seen, "new content");
    EXPECT_EQ(readText(file), "old");
    ASSERT_TRUE(atomicWriteFile(file, "").ok()); // empty content is allowed
    EXPECT_EQ(readText(file), "");
    EXPECT_EQ(readText(backupPath(file, 1)), "old");
}

TEST(AtomicSave, MissingFolderOrDirectoryTargetIsAnIoError) {
    TempDir dir;
    auto missing = saveDocument(dir.path() / "no-such-folder" / "k.fosp", revision(1));
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().code, ErrorCode::IoError);
    EXPECT_THAT(missing.error().message, HasSubstr("does not exist"));
    EXPECT_EQ(saveDocument(dir.path(), revision(1)).error().code, ErrorCode::IoError);
    EXPECT_TRUE(snapshot(dir.path()).empty());
}

#ifndef _WIN32
TEST(AtomicSave, ReadOnlyFolderLeavesOldFileIntact) {
    if (::geteuid() == 0) GTEST_SKIP() << "root ignores directory permissions";
    TempDir dir;
    const fs::path file = dir / "k.fosp";
    ASSERT_TRUE(saveDocument(file, revision(1)).ok());
    const std::string before = readText(file);
    fs::permissions(dir.path(), fs::perms::owner_read | fs::perms::owner_exec);
    auto r = saveDocument(file, revision(2));
    fs::permissions(dir.path(), fs::perms::owner_all);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::IoError);
    EXPECT_EQ(readText(file), before);
    EXPECT_EQ(snapshot(dir.path()).size(), 1u);
}
#endif

TEST(AtomicSave, UnsavableDocumentDoesNotTouchTheFile) {
    TempDir dir;
    const fs::path file = dir / "k.fosp";
    ASSERT_TRUE(saveDocument(file, revision(1)).ok());
    Document bad = revision(2);
    bad.payload["text"] = std::string("\xC3\x28"); // invalid UTF-8 sequence
    EXPECT_EQ(saveDocument(file, bad).error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(revisionOf(file), 1);
    EXPECT_EQ(snapshot(dir.path()).size(), 1u);
}

// ---------------------------------------------------------------- autosave / recovery

TEST(Recovery, AutosaveUsesTheNamingConventionAndLeavesTheDocumentAlone) {
    TempDir dir;
    const fs::path file = dir / "kitchen.fosp";
    ASSERT_TRUE(saveDocument(file, revision(1)).ok());
    const std::string before = readText(file);

    auto report = writeAutosave(file, revision(2));
    ASSERT_TRUE(report.ok()) << report.error().message;
    EXPECT_EQ(autosavePath(file).filename().string(), "kitchen.fosp.autosave");
    EXPECT_EQ(report.value().path, autosavePath(file));
    EXPECT_EQ(revisionOf(autosavePath(file)), 2);
    EXPECT_EQ(readText(file), before);
    // Repeated autosaves overwrite each other without creating backups.
    ASSERT_TRUE(writeAutosave(file, revision(3)).ok());
    EXPECT_EQ(revisionOf(autosavePath(file)), 3);
    EXPECT_EQ(snapshot(dir.path()).size(), 2u);

    EXPECT_TRUE(discardAutosave(file).ok());
    EXPECT_FALSE(fs::exists(autosavePath(file)));
    EXPECT_TRUE(discardAutosave(file).ok()); // already gone: still fine
}

TEST(Recovery, ListsCandidatesNewestFirst) {
    TempDir dir;
    const fs::path file = dir / "kitchen.fosp";
    EXPECT_TRUE(listRecoveryCandidates(file).empty());

    SaveOptions options;
    options.backupCount = 5;
    for (int n = 1; n <= 3; ++n) ASSERT_TRUE(saveDocument(file, revision(n), options).ok());
    ASSERT_TRUE(writeAutosave(file, revision(4)).ok());
    writeText(backupPath(file, 5), "stray backup with a gap before it");
    // Files that must be ignored: other documents, non-numeric suffixes, the document itself.
    writeText(dir / "kitchen.fosp.bakery", "x");
    writeText(dir / "kitchen.fosp.bak0", "x");
    writeText(dir / "other.fosp.bak1", "x");

    // Explicit timestamps: bak2 (oldest) < bak5 < bak1 < autosave (newest).
    const auto base = fs::file_time_type::clock::now();
    using std::chrono::seconds;
    fs::last_write_time(backupPath(file, 2), base - seconds{400});
    fs::last_write_time(backupPath(file, 5), base - seconds{300});
    fs::last_write_time(backupPath(file, 1), base - seconds{200});
    fs::last_write_time(autosavePath(file), base - seconds{100});

    const auto candidates = listRecoveryCandidates(file);
    ASSERT_EQ(candidates.size(), 4u);
    EXPECT_EQ(candidates[0].kind, RecoveryKind::Autosave);
    EXPECT_EQ(candidates[0].path, autosavePath(file));
    EXPECT_EQ(candidates[1].kind, RecoveryKind::Backup);
    EXPECT_EQ(candidates[1].backupIndex, 1);
    EXPECT_EQ(candidates[2].backupIndex, 5);
    EXPECT_EQ(candidates[3].backupIndex, 2);
    EXPECT_GT(candidates[0].modified, candidates[1].modified);

    // Equal timestamps: autosave first, then the lower backup index.
    for (const auto& c : candidates) fs::last_write_time(c.path, base);
    const auto tied = listRecoveryCandidates(file);
    ASSERT_EQ(tied.size(), 4u);
    EXPECT_EQ(tied[0].kind, RecoveryKind::Autosave);
    EXPECT_EQ(tied[1].backupIndex, 1);
    EXPECT_EQ(tied[2].backupIndex, 2);
    EXPECT_EQ(tied[3].backupIndex, 5);
}

TEST(Recovery, LoadsTheNewestValidCandidateAndReportsSkippedOnes) {
    TempDir dir;
    const fs::path file = dir / "kitchen.fosp";
    for (int n = 1; n <= 3; ++n) ASSERT_TRUE(saveDocument(file, revision(n)).ok()); // bak1 = 2, bak2 = 1
    ASSERT_TRUE(writeAutosave(file, revision(4)).ok());
    // The autosave was cut off by a crash; bak1 is fine.
    const std::string autosave = readText(autosavePath(file));
    writeText(autosavePath(file), autosave.substr(0, autosave.size() / 2));
    const auto base = fs::file_time_type::clock::now();
    fs::last_write_time(backupPath(file, 2), base - std::chrono::seconds{300});
    fs::last_write_time(backupPath(file, 1), base - std::chrono::seconds{200});
    fs::last_write_time(autosavePath(file), base - std::chrono::seconds{100});

    auto recovered = loadNewestValid(file, spec());
    ASSERT_TRUE(recovered.ok()) << recovered.error().message;
    EXPECT_EQ(recovered.value().loaded.document.payload["revision"], 2);
    EXPECT_EQ(recovered.value().source.kind, RecoveryKind::Backup);
    EXPECT_EQ(recovered.value().source.backupIndex, 1);
    ASSERT_EQ(recovered.value().skipped.size(), 1u);
    EXPECT_EQ(recovered.value().skipped[0].candidate.kind, RecoveryKind::Autosave);
    EXPECT_EQ(recovered.value().skipped[0].error.code, ErrorCode::ParseError);
}

TEST(Recovery, NoCandidateOrNoValidCandidateIsNotFound) {
    TempDir dir;
    const fs::path file = dir / "kitchen.fosp";
    auto none = loadNewestValid(file, spec());
    ASSERT_FALSE(none.ok());
    EXPECT_EQ(none.error().code, ErrorCode::NotFound);
    EXPECT_THAT(none.error().message, HasSubstr("No autosave or backup files exist"));

    writeText(autosavePath(file), "{ not json");
    writeText(backupPath(file, 1), "[]");
    auto invalid = loadNewestValid(file, spec());
    ASSERT_FALSE(invalid.ok());
    EXPECT_EQ(invalid.error().code, ErrorCode::NotFound);
    EXPECT_THAT(invalid.error().message, HasSubstr("None of the 2 autosave/backup files"));
    EXPECT_THAT(invalid.error().message, HasSubstr("kitchen.fosp.autosave"));
    EXPECT_THAT(invalid.error().message, HasSubstr("kitchen.fosp.bak1"));
}
