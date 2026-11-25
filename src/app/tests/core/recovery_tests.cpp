// SPDX-License-Identifier: AGPL-3.0-only
// Crash-recovery store: keys, sidecar parsing (robust to damage), write / list / remove.
#include "fos/app/recovery_store.hpp"
#include "fos/app/version.hpp"
#include "fos/project/file.hpp"
#include "test_support.hpp"

#include <fstream>

namespace fos::app::test {
namespace {

namespace fs = std::filesystem;

const QDateTime kT1 = QDateTime(QDate(2026, 3, 4), QTime(10, 20, 30, 400), Qt::UTC);

project::Project makeProject() {
    project::Metadata m;
    m.id = "1b4e28ba-2fa1-11d2-883f-0016d3cca427";
    m.name = "Kitchen";
    return project::newProject(m);
}

TEST(RecoveryKey, StableHashOfCleanAbsolutePath) {
    // python3: hashlib.sha256(b'/tmp/fos-recovery-key/k.fosp').hexdigest()[:16]
#ifndef _WIN32
    EXPECT_EQ(recoveryKey("/tmp/fos-recovery-key/k.fosp"), "65fc5d0872248282");
    EXPECT_EQ(recoveryKey("/tmp/fos-recovery-key/sub/../k.fosp"), "65fc5d0872248282"); // cleaned
    EXPECT_EQ(recoveryKey("/tmp/fos-recovery-key/./k.fosp"), "65fc5d0872248282");
#endif
    const std::string a = recoveryKey("/tmp/a.fosp"), b = recoveryKey("/tmp/b.fosp");
    EXPECT_NE(a, b);
    EXPECT_EQ(a.size(), 16u);
    EXPECT_EQ(a.find_first_not_of("0123456789abcdef"), std::string::npos);
}

TEST(RecoveryKey, UntitledSanitised) {
    EXPECT_EQ(untitledRecoveryKey("1b4e28ba-2fa1-11d2-883f-0016d3cca427"), "untitled-1b4e28ba-2fa1-11d2-883f-0016d3cca427");
    EXPECT_EQ(untitledRecoveryKey("../x/y z"), "untitled-xyz"); // no path separators survive
    EXPECT_EQ(untitledRecoveryKey(""), "untitled-unnamed");
}

TEST(RecoverySidecar, RoundTrip) {
    RecoverySidecar s{QStringLiteral("/home/u/Кухня.fosp"), QStringLiteral("Kitchen"), kT1, QStringLiteral("1.2.3")};
    const auto parsed = parseRecoverySidecar(serializeRecoverySidecar(s));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->originalPath, s.originalPath);
    EXPECT_EQ(parsed->projectName, s.projectName);
    EXPECT_EQ(parsed->savedAt, kT1); // millisecond precision kept
    EXPECT_EQ(parsed->appVersion, s.appVersion);
}

TEST(RecoverySidecar, DamagedInputIsRejectedNotCrashing) {
    const QByteArray full = serializeRecoverySidecar({QStringLiteral("/a.fosp"), {}, kT1, QStringLiteral("1")});
    for (qsizetype n = 0; n < full.size() - 1; ++n) // every truncation (the last byte is a newline)
        EXPECT_FALSE(parseRecoverySidecar(full.left(n)).has_value()) << n;
    EXPECT_FALSE(parseRecoverySidecar(QByteArray("\x00\xff\xfe binary", 10)).has_value());
    EXPECT_FALSE(parseRecoverySidecar("[1, 2]").has_value());
    EXPECT_FALSE(parseRecoverySidecar(R"({"kind":"other","savedAt":"2026-03-04T10:20:30Z"})").has_value());
    EXPECT_FALSE(parseRecoverySidecar(R"({"kind":"furnitureos-recovery"})").has_value()); // no time
    EXPECT_FALSE(parseRecoverySidecar(R"({"kind":"furnitureos-recovery","savedAt":"yesterday"})").has_value());
    EXPECT_FALSE(parseRecoverySidecar(R"({"kind":"furnitureos-recovery","savedAt":"2026-03-04T10:20:30Z","originalPath":7})")
                     .has_value());
    // Minimal valid one: optional fields empty, unknown keys and a newer version ignored.
    const auto minimal =
        parseRecoverySidecar(R"({"kind":"furnitureos-recovery","version":9,"x":1,"savedAt":"2026-03-04T12:20:30+02:00"})");
    ASSERT_TRUE(minimal.has_value());
    EXPECT_TRUE(minimal->originalPath.isEmpty());
    EXPECT_EQ(minimal->savedAt, QDateTime(QDate(2026, 3, 4), QTime(10, 20, 30), Qt::UTC)); // 12:20 at +02:00
}

TEST(RecoveryStore, WriteListFindRemove) {
    TempDir dir;
    const RecoveryStore store(dir.path / "recovery");
    EXPECT_TRUE(store.list().empty()); // directory does not exist yet
    EXPECT_FALSE(store.find("nope").has_value());

    project::Project p = makeProject();
    p.settings.backupCount = 5;
    const fs::path original = dir.path / "kitchen.fosp";
    const std::string key = recoveryKey(original);
    auto written = store.write(key, p, original, kT1);
    ASSERT_TRUE(written.ok()) << written.error().message;
    EXPECT_EQ(written.value(), dir.path / "recovery" / (key + ".fosp"));
    ASSERT_TRUE(store.write(key, p, original, kT1.addSecs(60)).ok()); // overwrite in place
    EXPECT_FALSE(fs::exists(formats::backupPath(store.fileFor(key), 1))); // no backups
    EXPECT_FALSE(fs::exists(original));                                  // user's file never written
    // The copy loads and keeps the project's own backup setting.
    auto loaded = project::loadProject(store.fileFor(key));
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    EXPECT_EQ(loaded.value().project.settings.backupCount, 5);

    ASSERT_TRUE(store.write("untitled-abc", p, {}, kT1).ok()); // older than the kitchen copy
    ASSERT_TRUE(store.write("untitled-aaa", p, {}, kT1).ok()); // same time: ordered by key
    std::ofstream(store.sidecarFor("orphan")) << "{}";          // sidecar without a copy: ignored
    std::ofstream(store.fileFor(key).string() + ".tmp-123") << "x"; // atomic-write temporary: ignored

    const auto entries = store.list();
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].key, key); // kT1 + 60 s
    EXPECT_TRUE(entries[0].sidecarValid);
    EXPECT_FALSE(entries[0].untitled());
    EXPECT_EQ(fs::path(entries[0].originalPath.toStdU16String()), original);
    EXPECT_EQ(entries[0].projectName, QStringLiteral("Kitchen"));
    EXPECT_EQ(entries[0].savedAt, kT1.addSecs(60));
    EXPECT_EQ(entries[0].appVersion, QString::fromLatin1(applicationVersion()));
    EXPECT_EQ(entries[1].key, "untitled-aaa");
    EXPECT_EQ(entries[2].key, "untitled-abc");
    EXPECT_TRUE(entries[2].untitled());

    // Damaged sidecar: still listed, time falls back to the file's modification time.
    std::ofstream(store.sidecarFor("untitled-abc"), std::ios::trunc) << "\x01garbage";
    auto damaged = store.find("untitled-abc");
    ASSERT_TRUE(damaged.has_value());
    EXPECT_FALSE(damaged->sidecarValid);
    EXPECT_TRUE(damaged->originalPath.isEmpty());
    EXPECT_TRUE(damaged->savedAt.isValid());
    // Missing sidecar: the same fallback.
    fs::remove(store.sidecarFor("untitled-aaa"));
    auto missing = store.find("untitled-aaa");
    ASSERT_TRUE(missing.has_value());
    EXPECT_FALSE(missing->sidecarValid);
    // Oversized sidecar (> 64 KiB) is not read.
    std::ofstream(store.sidecarFor(key), std::ios::trunc) << std::string(70 * 1024, ' ');
    EXPECT_FALSE(store.find(key)->sidecarValid);

    EXPECT_TRUE(store.remove(key));
    EXPECT_FALSE(fs::exists(store.fileFor(key)));
    EXPECT_FALSE(fs::exists(store.sidecarFor(key)));
    EXPECT_TRUE(store.remove("never-existed"));
    EXPECT_EQ(store.list().size(), 2u);
}

TEST(RecoveryStore, WriteFailsCleanlyWhenFolderCannotBeCreated) {
    TempDir dir;
    std::ofstream(dir.path / "file") << "x"; // a file where the folder should be
    const RecoveryStore store(dir.path / "file" / "recovery");
    auto r = store.write("k", makeProject(), {}, kT1);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("recovery folder"), std::string::npos);
}

} // namespace
} // namespace fos::app::test
