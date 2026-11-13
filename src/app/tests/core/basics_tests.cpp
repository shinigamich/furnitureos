// SPDX-License-Identifier: AGPL-3.0-only
// Window title, recent files, autosave decisions, recovery offers, command label text.
#include "fos/app/autosave.hpp"
#include "fos/app/command_text.hpp"
#include "fos/app/recent_files.hpp"
#include "fos/app/version.hpp"
#include "fos/app/window_title.hpp"
#include "test_support.hpp"

#include <QSettings>

#include <fstream>
#include <thread>

namespace fos::app::test {
namespace {

namespace fs = std::filesystem;

void touch(const fs::path& p, const std::string& text = "x") { std::ofstream(p) << text; }

TEST(WindowTitle, FileNameAndDirtyMarker) {
    EXPECT_EQ(windowTitle("/home/a/kitchen.fosp", false), QString::fromUtf8("kitchen.fosp — FurnitureOS"));
    EXPECT_EQ(windowTitle("/home/a/kitchen.fosp", true), QString::fromUtf8("kitchen.fosp* — FurnitureOS"));
    EXPECT_EQ(windowTitle({}, true), QString::fromUtf8("Untitled* — FurnitureOS"));
}

TEST(Version, MatchesProject) {
    EXPECT_STRNE(applicationVersion(), "");
    EXPECT_EQ(std::string(sourceUrl()).rfind("https://", 0), 0u);
}

TEST(RecentFiles, OrderDedupeLimitAndPersistence) {
    TempDir dir;
    RecentFiles recent(3);
    const QString d = QString::fromStdString(dir.path.string());
    recent.add(d + "/a.fosp");
    recent.add(d + "/b.fosp");
    recent.add(d + "/./a.fosp"); // same file -> moved to the front, not duplicated
    ASSERT_EQ(recent.entries().size(), 2);
    EXPECT_EQ(recent.entries()[0], d + "/a.fosp");
    EXPECT_EQ(recent.entries()[1], d + "/b.fosp");
    recent.add(d + "/c.fosp");
    recent.add(d + "/e.fosp"); // limit 3 -> the oldest (b) drops out
    EXPECT_EQ(recent.entries(), (QStringList{d + "/e.fosp", d + "/c.fosp", d + "/a.fosp"}));
    recent.add({});
    EXPECT_EQ(recent.entries().size(), 3);

    QSettings settings(d + "/settings.ini", QSettings::IniFormat);
    recent.save(settings);
    RecentFiles loaded(3);
    loaded.load(settings);
    EXPECT_EQ(loaded.entries(), recent.entries());

    // Only e exists on disk.
    touch(dir.path / "e.fosp");
    EXPECT_EQ(loaded.pruneMissing(), 2);
    EXPECT_EQ(loaded.entries(), QStringList{d + "/e.fosp"});
    loaded.remove(d + "/e.fosp");
    EXPECT_TRUE(loaded.entries().isEmpty());
}

TEST(Autosave, IntervalClamp) {
    EXPECT_EQ(autosaveIntervalMs(0), 0);
    EXPECT_EQ(autosaveIntervalMs(-3), 0);
    EXPECT_EQ(autosaveIntervalMs(5), 5 * 60 * 1000); // 300000 ms
    EXPECT_EQ(autosaveIntervalMs(1000), kMaxAutosaveMinutes * 60 * 1000);
}

TEST(Autosave, DueOnlyForUnsavedNewRevisions) {
    AutosaveState s;
    s.intervalMinutes = 5;
    s.revision = 7;
    EXPECT_FALSE(autosaveDue(s)); // clean
    s.dirty = true;
    EXPECT_TRUE(autosaveDue(s)); // never autosaved
    s.lastAutosavedRevision = 7;
    EXPECT_FALSE(autosaveDue(s)); // this revision is already in the autosave
    s.revision = 8;
    EXPECT_TRUE(autosaveDue(s));
    s.intervalMinutes = 0;
    EXPECT_FALSE(autosaveDue(s)); // disabled
}

TEST(Autosave, UntitledBaseAndNewest) {
    TempDir dir;
    EXPECT_EQ(untitledAutosaveBase(dir.path, "abc"), dir.path / "untitled-abc.fosp");
    EXPECT_FALSE(newestUntitledAutosave(dir.path).has_value());
    touch(dir.path / "untitled-old.fosp.autosave");
    touch(dir.path / "other.fosp.autosave"); // not an untitled autosave
    fs::last_write_time(dir.path / "untitled-old.fosp.autosave", fs::file_time_type::clock::now() - std::chrono::hours(1));
    touch(dir.path / "untitled-new.fosp.autosave");
    const auto newest = newestUntitledAutosave(dir.path);
    ASSERT_TRUE(newest.has_value());
    EXPECT_EQ(*newest, dir.path / "untitled-new.fosp");
    EXPECT_FALSE(newestUntitledAutosave(dir.path / "missing").has_value());
}

TEST(Autosave, RecoveryOfferOnlyWhenNewer) {
    TempDir dir;
    const fs::path doc = dir.path / "k.fosp";
    EXPECT_FALSE(recoveryOffer(doc).has_value()); // nothing at all
    touch(doc);
    EXPECT_FALSE(recoveryOffer(doc).has_value()); // no candidates
    touch(formats::autosavePath(doc));
    fs::last_write_time(formats::autosavePath(doc), fs::last_write_time(doc) - std::chrono::minutes(10));
    EXPECT_FALSE(recoveryOffer(doc).has_value()); // autosave older than the document
    fs::last_write_time(formats::autosavePath(doc), fs::last_write_time(doc) + std::chrono::minutes(10));
    auto offer = recoveryOffer(doc);
    ASSERT_TRUE(offer.has_value());
    EXPECT_EQ(offer->candidate.kind, formats::RecoveryKind::Autosave);
    EXPECT_FALSE(offer->documentMissing);
    fs::remove(doc);
    offer = recoveryOffer(doc);
    ASSERT_TRUE(offer.has_value());
    EXPECT_TRUE(offer->documentMissing);
}

TEST(CommandText, SubstitutesArgumentsAndItemNames) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    const auto label = project::CommandLabel{"Set {1} of {0} to {2}", {"cab1", "width", "900"}};
    EXPECT_EQ(commandText(label, s.project()), QStringLiteral("Set width of Base cabinet to 900"));
    // Unknown ids and unknown keys stay as they are.
    EXPECT_EQ(commandText({"Remove {0}", {"cab9"}}, s.project()), QStringLiteral("Remove cab9"));
    EXPECT_EQ(commandText({"No placeholders", {}}, s.project()), QStringLiteral("No placeholders"));
}

} // namespace
} // namespace fos::app::test
