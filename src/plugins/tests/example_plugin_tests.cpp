// SPDX-License-Identifier: AGPL-3.0-only
#include "example_materials.hpp"
#include "fos/plugins/manager.hpp"
#include "plugin_test_support.hpp"

using namespace fos;
using namespace fos::plugins;
using namespace fos::plugins::test;

namespace {
const fs::path kExamples = FOS_EXAMPLE_PLUGINS_DIR;
const fs::path kExampleDir = kExamples / std::string(example::kPluginId);
} // namespace

TEST(ExamplePlugin, CommittedPluginLoadsThroughTheManager) {
    PluginManagerOptions o;
    o.appVersion = parseSemVer(example::kAppVersion).value();
    o.searchDirs = {kExamples};
    PluginManager m(o);
    EXPECT_TRUE(m.discover().empty());
    const auto* rec = m.find(example::kPluginId);
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(rec->state, PluginState::Enabled) << allMessages(rec->diagnostics);
    EXPECT_TRUE(rec->diagnostics.empty()) << allMessages(rec->diagnostics); // no warnings either
    EXPECT_EQ(rec->manifest.license, "CC0-1.0");
    EXPECT_EQ(rec->manifest.localizedNames.count("uk"), 1u);

    formats::Library target;
    auto report = m.loadInto(target);
    ASSERT_EQ(report.loaded.size(), 1u) << allMessages(report.diagnostics);
    EXPECT_EQ(target.materials.size(), 5u);
    EXPECT_EQ(target.edgeBands.size(), 4u);
    EXPECT_TRUE(target.hardware.empty());
    for (const auto& mat : target.materials) {
        EXPECT_EQ(checkNamespacedId(example::kPluginId, mat.id.value), "");
        EXPECT_TRUE(mat.manufacturer.empty()) << mat.name; // generic, no real manufacturer
        EXPECT_EQ(mat.name.rfind("Generic ", 0), 0u) << mat.name;
    }
    for (const auto& eb : target.edgeBands) {
        EXPECT_TRUE(eb.manufacturer.empty());
        EXPECT_GE(eb.width, 18.0 + 4.0) << eb.name; // 22 mm tape covers an 18/19 mm board with overhang
    }
}

TEST(ExamplePlugin, CommittedLibraryIsReproducible) {
    // The committed file must be exactly what the real writer produces (so its checksum is right).
    TempDir tmp;
    auto written = example::writeLibrary(tmp / "fresh.foslib");
    ASSERT_TRUE(written.ok()) << written.error().message;
    const auto committed = readText(kExampleDir / std::string(example::kLibraryRelativePath));
    ASSERT_FALSE(committed.empty()) << "missing; run fos_plugins_make_example";
    EXPECT_EQ(committed, readText(tmp / "fresh.foslib"))
        << "examples library is stale; regenerate it with fos_plugins_make_example";
    auto imported = formats::importLibrary(kExampleDir / std::string(example::kLibraryRelativePath));
    ASSERT_TRUE(imported.ok()) << imported.error().message;
    EXPECT_EQ(imported.value().materials.size(), example::library().materials.size());
}
