// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/plugins/package.hpp"
#include "plugin_test_support.hpp"

#include <gmock/gmock.h>

using namespace fos;
using namespace fos::plugins;
using namespace fos::plugins::test;
using ::testing::HasSubstr;

TEST(Package, ValidPackageResolvesAndLoads) {
    TempDir tmp;
    const auto dir = makePlugin(tmp.path(), "org.example.boards");
    auto inspection = inspectPackage(dir);
    ASSERT_TRUE(inspection.ok()) << allMessages(inspection.diagnostics);
    const auto& pkg = *inspection.package;
    ASSERT_EQ(pkg.libraryFiles.size(), 1u);
    EXPECT_EQ(pkg.libraryFiles[0], fs::canonical(dir / "lib/main.foslib"));
    EXPECT_EQ(pkg.totalBytes, fs::file_size(dir / "lib/main.foslib"));
    auto lib = loadPackageLibraries(pkg);
    ASSERT_TRUE(lib.ok()) << lib.error().message;
    ASSERT_EQ(lib.value().materials.size(), 1u);
    EXPECT_EQ(lib.value().materials[0].id.value, "org.example.boards:board");
}

TEST(Package, RejectsTraversalAbsoluteAndSymlinkEscapes) {
    TempDir tmp;
    const auto dir = tmp / "org.example.evil";
    writeLibrary(tmp / "outside.foslib", namespacedLibrary("org.example.evil"));
    writeLibrary(dir / "inside/real.foslib", namespacedLibrary("org.example.evil"));
    writeText(dir / "plugin.json", "{}");

    // 1. '..' traversal (the file exists, but the path is refused before touching it).
    auto traversal = resolveInside(dir, "../outside.foslib");
    ASSERT_FALSE(traversal.ok());
    EXPECT_EQ(traversal.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_THAT(traversal.error().message, HasSubstr("contains '..'"));
    EXPECT_FALSE(resolveInside(dir, "inside/../../outside.foslib").ok());

    // 2. Absolute path to an existing file.
    auto absolute = resolveInside(dir, (tmp / "outside.foslib").generic_string());
    ASSERT_FALSE(absolute.ok());
    EXPECT_THAT(absolute.error().message, HasSubstr("absolute"));

    // 3. Symlink escapes: a file symlink and a directory symlink pointing outside.
    fs::create_symlink(tmp / "outside.foslib", dir / "link.foslib");
    fs::create_directories(tmp / "elsewhere");
    writeLibrary(tmp / "elsewhere/x.foslib", namespacedLibrary("org.example.evil"));
    fs::create_directory_symlink(tmp / "elsewhere", dir / "linkdir");
    for (const char* rel : {"link.foslib", "linkdir/x.foslib"}) {
        auto r = resolveInside(dir, rel);
        ASSERT_FALSE(r.ok()) << rel;
        EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);
        EXPECT_THAT(r.error().message, HasSubstr("outside the plugin directory"));
    }
    // A symlink that stays inside is fine.
    fs::create_symlink(dir / "inside/real.foslib", dir / "alias.foslib");
    auto alias = resolveInside(dir, "alias.foslib");
    ASSERT_TRUE(alias.ok()) << alias.error().message;
    EXPECT_EQ(alias.value(), fs::canonical(dir / "inside/real.foslib"));
    // Missing file and directory instead of file.
    EXPECT_EQ(resolveInside(dir, "missing.foslib").error().code, core::ErrorCode::NotFound);
    EXPECT_EQ(resolveInside(dir, "inside").error().code, core::ErrorCode::InvalidArgument);
}

TEST(Package, SymlinkEscapeInManifestMakesPackageInvalid) {
    TempDir tmp;
    writeLibrary(tmp / "secret/outside.foslib", namespacedLibrary("org.example.evil"));
    const auto dir = tmp / "org.example.evil";
    writeText(dir / "plugin.json", manifestJson("org.example.evil", "1.0.0", {"lib/main.foslib"}).dump());
    fs::create_directories(dir / "lib");
    fs::create_symlink(tmp / "secret/outside.foslib", dir / "lib/main.foslib");
    auto inspection = inspectPackage(dir);
    EXPECT_FALSE(inspection.ok());
    EXPECT_TRUE(hasCode(inspection.diagnostics, "plugins.package.bad_path"));
    EXPECT_THAT(allMessages(inspection.diagnostics), HasSubstr("/contributes/libraries/0: "));

    // plugin.json itself as a symlink to a file outside.
    const auto dir2 = tmp / "org.example.evil2";
    fs::create_directories(dir2);
    writeText(tmp / "secret/plugin.json", manifestJson("org.example.evil2").dump());
    fs::create_symlink(tmp / "secret/plugin.json", dir2 / "plugin.json");
    auto i2 = inspectPackage(dir2);
    EXPECT_FALSE(i2.ok());
    EXPECT_TRUE(hasCode(i2.diagnostics, "plugins.package.bad_manifest_path")) << allMessages(i2.diagnostics);
}

TEST(Package, SizeCapsAreCheckedBeforeReading) {
    TempDir tmp;
    const auto dir = makePlugin(tmp.path(), "org.example.big");
    const auto libSize = fs::file_size(dir / "lib/main.foslib");
    const auto manifestSize = fs::file_size(dir / "plugin.json");

    PluginLimits perFile;
    perFile.maxLibraryBytes = libSize - 1;
    auto a = inspectPackage(dir, perFile);
    EXPECT_FALSE(a.ok());
    EXPECT_TRUE(hasCode(a.diagnostics, "plugins.package.file_too_large")) << allMessages(a.diagnostics);
    EXPECT_THAT(allMessages(a.diagnostics), HasSubstr(std::to_string(libSize) + " bytes"));

    PluginLimits exact;
    exact.maxLibraryBytes = libSize; // equal to the limit is allowed
    exact.maxTotalBytes = libSize;
    EXPECT_TRUE(inspectPackage(dir, exact).ok());

    PluginLimits total;
    total.maxTotalBytes = libSize - 1;
    auto b = inspectPackage(dir, total);
    EXPECT_TRUE(hasCode(b.diagnostics, "plugins.package.too_large")) << allMessages(b.diagnostics);

    PluginLimits manifest;
    manifest.maxManifestBytes = manifestSize - 1;
    auto c = inspectPackage(dir, manifest);
    EXPECT_FALSE(c.ok());
    EXPECT_TRUE(hasCode(c.diagnostics, "plugins.manifest.too_large")) << allMessages(c.diagnostics);

    auto bounded = readBounded(dir / "plugin.json", manifestSize - 1);
    ASSERT_FALSE(bounded.ok());
    EXPECT_EQ(bounded.error().code, core::ErrorCode::ConstraintViolation);
    EXPECT_EQ(readBounded(dir / "plugin.json", manifestSize).value().size(), manifestSize);

    PluginLimits entries;
    entries.maxEntries = 1; // the library has 1 material + 1 edge band
    auto lib = loadPackageLibraries(*inspectPackage(dir).package, entries);
    ASSERT_FALSE(lib.ok());
    EXPECT_THAT(lib.error().message, HasSubstr("more than 1 library entries"));
}

TEST(Package, MissingManifestOrLibraryOrNotADirectory) {
    TempDir tmp;
    fs::create_directories(tmp / "org.example.empty");
    auto a = inspectPackage(tmp / "org.example.empty");
    EXPECT_TRUE(hasCode(a.diagnostics, "plugins.package.no_manifest")) << allMessages(a.diagnostics);
    writeText(tmp / "file", "x");
    EXPECT_TRUE(hasCode(inspectPackage(tmp / "file").diagnostics, "plugins.package.not_a_directory"));
    const auto dir = makePlugin(tmp.path(), "org.example.nolib");
    fs::remove(dir / "lib/main.foslib");
    auto b = inspectPackage(dir);
    EXPECT_FALSE(b.ok());
    EXPECT_THAT(allMessages(b.diagnostics), HasSubstr("\"lib/main.foslib\" does not exist"));
}

TEST(Package, NamespacingIsEnforced) {
    EXPECT_EQ(checkNamespacedId("org.ex", "org.ex:oak"), "");
    EXPECT_NE(checkNamespacedId("org.ex", "org.ex:"), "");
    EXPECT_NE(checkNamespacedId("org.ex", "oak"), "");
    EXPECT_NE(checkNamespacedId("org.ex", "org.ex.other:oak"), "");
    EXPECT_NE(checkNamespacedId("org.ex", "org.exo:oak"), "");

    TempDir tmp;
    formats::Library lib = namespacedLibrary("org.example.ns");
    lib.edgeBands.push_back(edgeBand("eb.white", "User-style id"));
    const auto dir = makePlugin(tmp.path(), "org.example.ns", "1.0.0", &lib);
    auto loaded = loadPackageLibraries(*inspectPackage(dir).package);
    ASSERT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.error().message,
              "Plugin 'org.example.ns', library \"lib/main.foslib\", /payload/edgeBands/1/id: id \"eb.white\" must start "
              "with \"org.example.ns:\" followed by a local id, so that plugins cannot overwrite user or other plugins' entries.");
}

TEST(Package, DuplicateIdsAcrossFilesOfOnePluginAndCorruptFiles) {
    TempDir tmp;
    const auto dir = tmp / "org.example.dup";
    writeText(dir / "plugin.json", manifestJson("org.example.dup", "1.0.0", {"a.foslib", "b.foslib"}).dump());
    writeLibrary(dir / "a.foslib", namespacedLibrary("org.example.dup"));
    writeLibrary(dir / "b.foslib", namespacedLibrary("org.example.dup"));
    auto r = loadPackageLibraries(*inspectPackage(dir).package);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("b.foslib\", /payload/materials/0/id: id \"org.example.dup:board\" is already used"));

    // Tampered payload -> checksum mismatch from formats::importLibrary.
    auto text = readText(dir / "b.foslib");
    text.replace(text.find("Board org.example.dup"), 5, "Bored");
    writeText(dir / "b.foslib", text);
    auto c = loadPackageLibraries(*inspectPackage(dir).package);
    ASSERT_FALSE(c.ok());
    EXPECT_THAT(c.error().message, HasSubstr("library \"b.foslib\" cannot be loaded"));
}
