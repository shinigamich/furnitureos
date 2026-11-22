// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/plugins/manager.hpp"
#include "plugin_test_support.hpp"

#include <gmock/gmock.h>

using namespace fos;
using namespace fos::plugins;
using namespace fos::plugins::test;
using ::testing::HasSubstr;

namespace {

formats::ComponentFile componentFile(const std::string& componentId) {
    formats::ComponentRecord rec;
    rec.meta.id = componentId;
    rec.meta.name = "Shelf board";
    rec.meta.category = "Panels";
    rec.library.materials.push_back(material("mat.oak18", "Oak", 18));
    rec.definition = Json{{"type", "panel"}};
    formats::ComponentFile file;
    file.components.push_back(rec);
    return file;
}

fs::path makeComponentPlugin(const fs::path& parent, const std::string& id, const std::string& componentId) {
    const fs::path dir = makePlugin(parent, id);
    Json m = manifestJson(id);
    m["contributes"]["components"] = Json::array({"components/shelf.foscomp"});
    writeText(dir / "plugin.json", m.dump(2));
    fs::create_directories(dir / "components");
    formats::SaveOptions o;
    o.backupCount = 0;
    auto r = formats::exportComponentFile(dir / "components/shelf.foscomp", componentFile(componentId), {}, o);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    return dir;
}

PluginManagerOptions options(const fs::path& dir) {
    PluginManagerOptions o;
    o.appVersion = parseSemVer("0.5.0").value();
    o.searchDirs = {dir};
    return o;
}

TEST(ComponentPlugins, ContributedComponentLoadsWithItsPlugin) {
    TempDir tmp;
    makeComponentPlugin(tmp.path(), "org.example.parts", "org.example.parts:shelf");
    PluginManager mgr(options(tmp.path()));
    mgr.discover();
    formats::Library target;
    const auto report = mgr.loadInto(target);
    ASSERT_EQ(report.loaded.size(), 1u) << allMessages(report.diagnostics);
    EXPECT_EQ(report.loaded[0].componentCount, 1u);
    ASSERT_EQ(report.components.size(), 1u);
    EXPECT_EQ(report.components[0].pluginId, "org.example.parts");
    EXPECT_EQ(report.components[0].file, "components/shelf.foscomp");
    EXPECT_EQ(report.components[0].record.meta.name, "Shelf board");
    // Embedded entries are not merged into the application library by loading.
    EXPECT_EQ(target.materials.size(), 1u);
}

TEST(ComponentPlugins, ComponentIdsMustBeNamespacedAndPathsSafe) {
    TempDir tmp;
    makeComponentPlugin(tmp.path(), "org.example.bad", "shelf");
    PluginManager mgr(options(tmp.path()));
    mgr.discover();
    formats::Library target;
    const auto report = mgr.loadInto(target);
    EXPECT_TRUE(report.loaded.empty());
    EXPECT_TRUE(target.materials.empty()); // all-or-nothing
    EXPECT_THAT(allMessages(report.diagnostics), HasSubstr("/payload/components/0/id"));
    EXPECT_THAT(allMessages(report.diagnostics), HasSubstr("org.example.bad:"));

    Json m = manifestJson("org.example.paths");
    m["contributes"]["components"] = Json::array({"../escape.foscomp", "lib/main.foslib"});
    const auto v = parseManifest(m.dump());
    EXPECT_FALSE(v.ok());
    EXPECT_THAT(allMessages(v.diagnostics), HasSubstr("/contributes/components/0"));
    EXPECT_THAT(allMessages(v.diagnostics), HasSubstr("must name a .foscomp component file"));
}

} // namespace
