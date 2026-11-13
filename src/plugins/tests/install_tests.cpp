// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/plugins/manager.hpp"
#include "plugin_test_support.hpp"

#include <gmock/gmock.h>

using namespace fos;
using namespace fos::plugins;
using namespace fos::plugins::test;
using ::testing::HasSubstr;

namespace {

struct Fixture {
    TempDir tmp;
    fs::path source = tmp / "downloads";
    fs::path root = tmp / "user-plugins";
    PluginManager manager{[this] {
        PluginManagerOptions o;
        o.appVersion = parseSemVer("0.5.0").value();
        o.searchDirs = {root};
        o.installDir = root;
        return o;
    }()};
};

} // namespace

TEST(Install, CopiesOnlyValidatedFilesAndIsDiscoverable) {
    Fixture f;
    const auto src = makePlugin(f.source, "org.example.inst");
    writeText(src / "LICENSE", "CC0");
    writeText(src / "notes/secret.txt", "not part of the package");
    writeText(src / "run.sh", "#!/bin/sh\necho hi\n");
    auto r = f.manager.install(src);
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().id, "org.example.inst");
    EXPECT_EQ(r.value().destination, fs::canonical(f.root) / "org.example.inst");
    EXPECT_FALSE(r.value().replacedVersion.has_value());
    EXPECT_EQ(r.value().copiedFiles, (std::vector<std::string>{"LICENSE", "lib/main.foslib", "plugin.json"}));
    EXPECT_FALSE(fs::exists(f.root / "org.example.inst/run.sh"));
    EXPECT_FALSE(fs::exists(f.root / "org.example.inst/notes"));
    // No staging leftovers.
    std::size_t entries = 0;
    for ([[maybe_unused]] const auto& e : fs::directory_iterator(f.root)) ++entries;
    EXPECT_EQ(entries, 1u);
    f.manager.discover();
    ASSERT_NE(f.manager.find("org.example.inst"), nullptr);
    formats::Library target;
    EXPECT_EQ(f.manager.loadInto(target).loaded.size(), 1u);
}

TEST(Install, RefusesInvalidIncompatibleAndNonNamespacedPackages) {
    Fixture f;
    const auto bad = makePlugin(f.source, "org.example.perm");
    Json j = manifestJson("org.example.perm");
    j["permissions"] = Json::array({"data", "filesystem"});
    writeText(bad / "plugin.json", j.dump());
    auto a = f.manager.install(bad);
    ASSERT_FALSE(a.ok());
    EXPECT_THAT(a.error().message, HasSubstr("permission \"filesystem\" is not granted"));

    const auto future = makePlugin(f.source, "org.example.future");
    Json k = manifestJson("org.example.future");
    k["compatibility"]["minAppVersion"] = "3.0.0";
    writeText(future / "plugin.json", k.dump());
    auto b = f.manager.install(future);
    ASSERT_FALSE(b.ok());
    EXPECT_EQ(b.error().code, core::ErrorCode::Unsupported);

    formats::Library plain;
    plain.materials.push_back(material("oak"));
    const auto ns = makePlugin(f.source, "org.example.ns", "1.0.0", &plain);
    auto c = f.manager.install(ns);
    ASSERT_FALSE(c.ok());
    EXPECT_THAT(c.error().message, HasSubstr("must start with \"org.example.ns:\""));

    const auto esc = makePlugin(f.source, "org.example.esc");
    fs::remove(esc / "lib/main.foslib");
    writeLibrary(f.tmp / "outside.foslib", namespacedLibrary("org.example.esc"));
    fs::create_symlink(f.tmp / "outside.foslib", esc / "lib/main.foslib");
    EXPECT_FALSE(f.manager.install(esc).ok());

    EXPECT_FALSE(fs::exists(f.root / "org.example.perm"));
    EXPECT_FALSE(fs::exists(f.root / "org.example.future"));
    EXPECT_FALSE(fs::exists(f.root / "org.example.ns"));
    EXPECT_FALSE(fs::exists(f.root / "org.example.esc"));
}

TEST(Install, RefusesToReplaceNewerVersionUnlessAsked) {
    Fixture f;
    ASSERT_TRUE(f.manager.install(makePlugin(f.source / "v2", "org.example.v", "2.0.0")).ok());
    const auto v1 = makePlugin(f.source / "v1", "org.example.v", "1.5.0");
    auto refused = f.manager.install(v1);
    ASSERT_FALSE(refused.ok());
    EXPECT_EQ(refused.error().code, core::ErrorCode::AlreadyExists);
    EXPECT_THAT(refused.error().message, HasSubstr("'org.example.v' 2.0.0 is installed, which is newer than 1.5.0"));
    EXPECT_THAT(readText(f.root / "org.example.v/plugin.json"), HasSubstr("\"2.0.0\""));

    InstallOptions force;
    force.overwriteNewer = true;
    auto downgraded = f.manager.install(v1, force);
    ASSERT_TRUE(downgraded.ok()) << downgraded.error().message;
    EXPECT_EQ(downgraded.value().replacedVersion.value(), "2.0.0");
    EXPECT_THAT(readText(f.root / "org.example.v/plugin.json"), HasSubstr("\"1.5.0\""));

    // Upgrade (and same version) replace without asking.
    auto upgraded = f.manager.install(makePlugin(f.source / "v3", "org.example.v", "3.0.0"));
    ASSERT_TRUE(upgraded.ok()) << upgraded.error().message;
    EXPECT_EQ(upgraded.value().replacedVersion.value(), "1.5.0");
    EXPECT_TRUE(f.manager.install(f.source / "v3/org.example.v").ok());
    // Unreadable installed manifest needs overwriteNewer.
    writeText(f.root / "org.example.v/plugin.json", "garbage");
    EXPECT_FALSE(f.manager.install(f.source / "v3/org.example.v").ok());
    EXPECT_TRUE(f.manager.install(f.source / "v3/org.example.v", force).ok());
    // Installing the installed directory onto itself is refused.
    EXPECT_FALSE(f.manager.install(f.root / "org.example.v").ok());
}

TEST(Remove, DeletesOnlyVerifiedPluginDirectoriesInsideTheRoot) {
    Fixture f;
    ASSERT_TRUE(f.manager.install(makePlugin(f.source, "org.example.rm")).ok());
    // A directory outside the root that must survive every attempt below.
    const auto victim = makePlugin(f.tmp / "elsewhere", "org.example.victim");

    for (const char* bad : {"../elsewhere/org.example.victim", "/etc", "", "org", "org.example.rm/../x"}) {
        auto r = f.manager.remove(bad);
        ASSERT_FALSE(r.ok()) << bad;
        EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument) << bad;
    }
    // Symlink inside the root pointing at an outside plugin: refused, target untouched.
    fs::create_directory_symlink(victim, f.root / "org.example.victim");
    auto link = f.manager.remove("org.example.victim");
    ASSERT_FALSE(link.ok());
    EXPECT_THAT(link.error().message, HasSubstr("not a plain directory"));
    EXPECT_TRUE(fs::exists(victim / "plugin.json"));
    fs::remove(f.root / "org.example.victim");
    // Directory without plugin.json, and directory whose manifest names another plugin.
    fs::create_directories(f.root / "org.example.notaplugin/data");
    EXPECT_THAT(f.manager.remove("org.example.notaplugin").error().message, HasSubstr("has no plugin.json"));
    writeText(f.root / "org.example.other/plugin.json", manifestJson("org.example.different").dump());
    EXPECT_THAT(f.manager.remove("org.example.other").error().message, HasSubstr("contains plugin 'org.example.different'"));
    EXPECT_EQ(f.manager.remove("org.example.absent").error().code, core::ErrorCode::NotFound);

    // A symlink inside the plugin directory is removed as a link; its target survives.
    fs::create_directory_symlink(victim, f.root / "org.example.rm/link");
    auto ok = f.manager.remove("org.example.rm");
    ASSERT_TRUE(ok.ok()) << ok.error().message;
    EXPECT_FALSE(fs::exists(f.root / "org.example.rm"));
    EXPECT_TRUE(fs::exists(victim / "plugin.json"));
    EXPECT_TRUE(fs::exists(victim / "lib/main.foslib"));
    EXPECT_TRUE(fs::exists(f.root / "org.example.notaplugin"));
}

TEST(Install, NoInstallDirConfigured) {
    TempDir tmp;
    PluginManager m(PluginManagerOptions{});
    EXPECT_FALSE(m.install(makePlugin(tmp.path(), "org.example.x")).ok());
    EXPECT_FALSE(m.remove("org.example.x").ok());
}
