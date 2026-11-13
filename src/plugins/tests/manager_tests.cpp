// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/plugins/manager.hpp"
#include "plugin_test_support.hpp"

#include <gmock/gmock.h>

using namespace fos;
using namespace fos::plugins;
using namespace fos::plugins::test;
using ::testing::HasSubstr;

namespace {

PluginManagerOptions optionsFor(std::vector<fs::path> dirs, const char* app = "0.5.0") {
    PluginManagerOptions o;
    o.appVersion = parseSemVer(app).value();
    o.searchDirs = std::move(dirs);
    return o;
}

std::vector<std::string> describe(const PluginManager& m) {
    std::vector<std::string> out;
    for (const auto& r : m.plugins())
        out.push_back(r.directory.parent_path().filename().string() + "/" + r.directory.filename().string() + "=" +
                      toString(r.state));
    return out;
}

void setCompatibility(const fs::path& dir, const char* minV, const char* maxV) {
    Json j = Json::parse(readText(dir / "plugin.json"));
    j["compatibility"] = Json{{"minAppVersion", minV}};
    if (maxV) j["compatibility"]["maxAppVersion"] = maxV;
    writeText(dir / "plugin.json", j.dump());
}

} // namespace

TEST(Manager, DiscoveryIsDeterministicAndSkipsHiddenAndFiles) {
    TempDir tmp;
    const auto a = tmp / "a";
    const auto b = tmp / "b";
    // Created in non-sorted order; discovery sorts names byte-wise within each dir.
    makePlugin(b, "org.zeta.one");
    makePlugin(a, "org.zeta.two");
    makePlugin(a, "org.alpha.one");
    makePlugin(a, "org.Beta"); // invalid id, upper-case B sorts before 'a'
    makePlugin(a, ".staging-org.x-1");
    writeText(a / "notes.txt", "ignored");
    PluginManager m(optionsFor({a, tmp / "missing", b}));
    const auto d = m.discover();
    ASSERT_EQ(d.size(), 1u);
    EXPECT_EQ(d[0].code, "plugins.discovery.search_dir_missing");
    EXPECT_EQ(d[0].severity, core::Severity::Info);
    const std::vector<std::string> expected = {"a/org.Beta=invalid", "a/org.alpha.one=enabled",
                                               "a/org.zeta.two=enabled", "b/org.zeta.one=enabled"};
    EXPECT_EQ(describe(m), expected);
    EXPECT_EQ(m.plugins()[3].searchDirIndex, 2u);
    // Repeating discovery gives the same result.
    m.discover();
    EXPECT_EQ(describe(m), expected);
}

TEST(Manager, DirectoryMustBeNamedAfterId) {
    TempDir tmp;
    const auto dir = makePlugin(tmp.path(), "org.example.real");
    fs::rename(dir, tmp / "renamed");
    PluginManager m(optionsFor({tmp.path()}));
    m.discover();
    ASSERT_EQ(m.plugins().size(), 1u);
    EXPECT_EQ(m.plugins()[0].state, PluginState::Invalid);
    EXPECT_TRUE(hasCode(m.plugins()[0].diagnostics, "plugins.discovery.directory_name"));
}

TEST(Manager, IncompatibleAppVersionIsReportedAndNotLoaded) {
    TempDir tmp;
    setCompatibility(makePlugin(tmp.path(), "org.example.future"), "1.0.0", nullptr);
    setCompatibility(makePlugin(tmp.path(), "org.example.old"), "0.1.0", "0.4.9");
    setCompatibility(makePlugin(tmp.path(), "org.example.edge"), "0.5.0", "0.5.0");
    PluginManager m(optionsFor({tmp.path()}, "0.5.0"));
    m.discover();
    EXPECT_EQ(describe(m), (std::vector<std::string>{"" + tmp.path().filename().string() + "/org.example.edge=enabled",
                                                     tmp.path().filename().string() + "/org.example.future=incompatible",
                                                     tmp.path().filename().string() + "/org.example.old=incompatible"}));
    EXPECT_THAT(allMessages(m.plugins()[1].diagnostics),
                HasSubstr("Plugin 'org.example.future' 1.0.0 requires FurnitureOS >= 1.0.0; this is 0.5.0. It was not loaded."));
    EXPECT_THAT(allMessages(m.plugins()[2].diagnostics), HasSubstr(">= 0.1.0 and <= 0.4.9"));
    formats::Library target;
    auto report = m.loadInto(target);
    ASSERT_EQ(report.loaded.size(), 1u);
    EXPECT_EQ(report.loaded[0].id, "org.example.edge");
}

TEST(Manager, DuplicateIdsHigherVersionWinsOthersShadowed) {
    TempDir tmp;
    const auto sys = tmp / "system";
    const auto user = tmp / "user";
    const auto extra = tmp / "extra";
    makePlugin(sys, "org.example.dup", "1.2.0");
    makePlugin(user, "org.example.dup", "1.10.0"); // 1.10.0 > 1.2.0 numerically
    makePlugin(extra, "org.example.dup", "2.0.0");  // highest, but incompatible
    setCompatibility(extra / "org.example.dup", "9.0.0", nullptr);
    makePlugin(sys, "org.example.tie", "1.0.0+sys");
    makePlugin(user, "org.example.tie", "1.0.0+user"); // equal precedence -> earlier dir (sys) wins
    PluginManager m(optionsFor({sys, user, extra}));
    m.discover();
    EXPECT_EQ(describe(m), (std::vector<std::string>{"system/org.example.dup=shadowed", "system/org.example.tie=enabled",
                                                     "user/org.example.dup=enabled", "user/org.example.tie=shadowed",
                                                     "extra/org.example.dup=incompatible"}));
    EXPECT_THAT(allMessages(m.plugins()[0].diagnostics),
                HasSubstr("'org.example.dup' 1.2.0 in '" + (sys / "org.example.dup").string() +
                          "' is shadowed by version 1.10.0 in '" + (user / "org.example.dup").string() + "'"));
    ASSERT_NE(m.find("org.example.dup"), nullptr);
    EXPECT_EQ(m.find("org.example.dup")->manifest.version.toString(), "1.10.0");
    EXPECT_EQ(m.find("org.example.tie")->directory, sys / "org.example.tie");
}

TEST(Manager, DisabledListInAndOut) {
    TempDir tmp;
    makePlugin(tmp.path(), "org.example.a");
    makePlugin(tmp.path(), "org.example.b");
    PluginManager m(optionsFor({tmp.path()}));
    m.setDisabledIds({"org.example.b", "org.unknown.x", "org.example.b"});
    EXPECT_EQ(m.disabledIds(), (std::vector<std::string>{"org.example.b", "org.unknown.x"}));
    m.discover();
    EXPECT_EQ(m.find("org.example.b")->state, PluginState::Disabled);
    formats::Library target;
    auto report = m.loadInto(target);
    ASSERT_EQ(report.loaded.size(), 1u);
    EXPECT_EQ(report.loaded[0].id, "org.example.a");
    EXPECT_EQ(target.materials.size(), 1u);
    m.setEnabled("org.example.b", true);
    EXPECT_EQ(m.disabledIds(), std::vector<std::string>{"org.unknown.x"});
    EXPECT_EQ(m.find("org.example.b")->state, PluginState::Enabled);
    m.setEnabled("org.example.a", false); // already Loaded: stays loaded, id recorded for next session
    EXPECT_EQ(m.disabledIds(), (std::vector<std::string>{"org.example.a", "org.unknown.x"}));
    auto second = m.loadInto(target);
    ASSERT_EQ(second.loaded.size(), 1u);
    EXPECT_EQ(second.loaded[0].id, "org.example.b");
}

TEST(Manager, BrokenPluginNeverAffectsOthers) {
    TempDir tmp;
    makePlugin(tmp.path(), "org.example.good");
    const auto bad = makePlugin(tmp.path(), "org.example.bad");
    makePlugin(tmp.path(), "org.example.worse");
    writeText(tmp / "org.example.broken/plugin.json", "{not json"); // invalid from the start
    PluginManager m(optionsFor({tmp.path()}));
    m.discover();
    EXPECT_EQ(m.plugins()[0].state, PluginState::Enabled); // order: bad, broken, good, worse
    EXPECT_EQ(m.plugins()[1].manifest.id, "");
    EXPECT_EQ(m.plugins()[1].state, PluginState::Invalid);
    // Corrupt after discovery: one checksum mismatch, one non-namespaced id.
    auto text = readText(bad / "lib/main.foslib");
    text.replace(text.find("Board org.example.bad"), 5, "Bored");
    writeText(bad / "lib/main.foslib", text);
    formats::Library worse;
    worse.materials.push_back(material("user-oak"));
    writeLibrary(tmp / "org.example.worse/lib/main.foslib", worse);

    formats::Library target;
    auto report = m.loadInto(target);
    ASSERT_EQ(report.loaded.size(), 1u);
    EXPECT_EQ(report.loaded[0].id, "org.example.good");
    ASSERT_EQ(target.materials.size(), 1u);
    EXPECT_EQ(target.materials[0].id.value, "org.example.good:board");
    EXPECT_EQ(m.find("org.example.bad")->state, PluginState::LoadFailed);
    EXPECT_EQ(m.find("org.example.worse")->state, PluginState::LoadFailed);
    ASSERT_EQ(report.diagnostics.size(), 2u) << allMessages(report.diagnostics);
    EXPECT_EQ(report.diagnostics[0].entityId, "org.example.bad"); // sorted by id
    EXPECT_THAT(report.diagnostics[1].message, HasSubstr("\"user-oak\" must start with \"org.example.worse:\""));
}

TEST(Manager, PluginsCannotOverwriteExistingEntries) {
    TempDir tmp;
    makePlugin(tmp.path(), "org.example.p");
    formats::Library target;
    target.materials.push_back(material("user-1", "User board"));
    // An entry with the plugin's id already in the target (e.g. saved in a project) is kept.
    target.materials.push_back(material("org.example.p:board", "Edited by the user", 19.0));
    PluginManager m(optionsFor({tmp.path()}));
    m.discover();
    auto report = m.loadInto(target);
    ASSERT_EQ(report.loaded.size(), 1u);
    EXPECT_EQ(report.loaded[0].merge.count(formats::MergeAction::Skipped), 1u);
    EXPECT_EQ(report.loaded[0].merge.count(formats::MergeAction::Added), 1u); // the edge band
    ASSERT_EQ(target.materials.size(), 2u);
    EXPECT_EQ(target.materials[1].name, "Edited by the user");
    EXPECT_DOUBLE_EQ(target.materials[1].thickness, 19.0);
    ASSERT_EQ(target.edgeBands.size(), 1u);
    EXPECT_TRUE(hasCode(report.diagnostics, "plugins.load.skipped_existing"));
}

TEST(Manager, PluginChangedOrRemovedAfterDiscoveryFailsCleanly) {
    TempDir tmp;
    const auto a = makePlugin(tmp.path(), "org.example.a");
    makePlugin(tmp.path(), "org.example.b");
    PluginManager m(optionsFor({tmp.path()}));
    m.discover();
    writeText(a / "plugin.json", manifestJson("org.example.a", "2.0.0").dump());
    fs::remove_all(tmp / "org.example.b");
    formats::Library target;
    auto report = m.loadInto(target);
    EXPECT_TRUE(report.loaded.empty());
    EXPECT_TRUE(hasCode(report.diagnostics, "plugins.load.changed")) << allMessages(report.diagnostics);
    EXPECT_TRUE(hasCode(report.diagnostics, "plugins.load.invalid")) << allMessages(report.diagnostics);
    EXPECT_TRUE(target.materials.empty());
}
