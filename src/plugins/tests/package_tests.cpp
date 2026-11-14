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
    EXPECT_TRUE(fs::equivalent(pkg.libraryFiles[0], dir / "lib/main.foslib"));
    EXPECT_TRUE(pkg.libraryFiles[0].is_absolute());
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

    // 2. Absolute path to an existing file ("/tmp/..." on POSIX, "C:/Users/..." on Windows).
    auto absolute = resolveInside(dir, (tmp / "outside.foslib").generic_string());
    ASSERT_FALSE(absolute.ok());
    EXPECT_THAT(absolute.error().message, HasSubstr("absolute"));
    // The native spelling too ("C:\\Users\\..." on Windows).
    EXPECT_FALSE(resolveInside(dir, (tmp / "outside.foslib").string()).ok());

    // Missing file and directory instead of file.
    EXPECT_EQ(resolveInside(dir, "missing.foslib").error().code, core::ErrorCode::NotFound);
    EXPECT_EQ(resolveInside(dir, "inside").error().code, core::ErrorCode::InvalidArgument);

    // 3. Link escapes. First a directory link (on Windows a junction when symbolic links need
    // privileges: junctions never do, so this part runs on every real Windows system).
    fs::create_directories(tmp / "elsewhere");
    writeLibrary(tmp / "elsewhere/x.foslib", namespacedLibrary("org.example.evil"));
    if (const auto why = tryCreateLink(tmp / "elsewhere", dir / "linkdir", true); !why.empty()) GTEST_SKIP() << why;
    auto viaDir = resolveInside(dir, "linkdir/x.foslib");
    ASSERT_FALSE(viaDir.ok());
    EXPECT_EQ(viaDir.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_THAT(viaDir.error().message, HasSubstr("outside the plugin directory"));
    // Then a file symbolic link.
    if (const auto why = tryCreateLink(tmp / "outside.foslib", dir / "link.foslib", false); !why.empty()) GTEST_SKIP() << why;
    auto viaFile = resolveInside(dir, "link.foslib");
    ASSERT_FALSE(viaFile.ok());
    EXPECT_EQ(viaFile.error().code, core::ErrorCode::InvalidArgument);
    EXPECT_THAT(viaFile.error().message, HasSubstr("outside the plugin directory"));
    // A link that stays inside is fine.
    if (const auto why = tryCreateLink(dir / "inside/real.foslib", dir / "alias.foslib", false); !why.empty()) GTEST_SKIP() << why;
    auto alias = resolveInside(dir, "alias.foslib");
    ASSERT_TRUE(alias.ok()) << alias.error().message;
    EXPECT_TRUE(fs::equivalent(alias.value(), dir / "inside/real.foslib"));
}

TEST(Package, WindowsSpecificEscapesAreRefusedOnEveryPlatform) {
    // These spellings are harmless file names on Linux but escape, alias or open devices on
    // Windows; a package must mean the same everywhere, so they are refused everywhere,
    // before the file system is touched.
    TempDir tmp;
    const auto dir = tmp / "org.example.win";
    writeLibrary(dir / "lib/main.foslib", namespacedLibrary("org.example.win"));
    struct Case {
        const char* path;
        const char* reason;
    };
    for (const Case& c : {Case{"C:/Windows/win.ini", "absolute"},
                          Case{"c:\\Windows\\win.ini", "absolute"},
                          Case{"C:lib/main.foslib", "drive-relative"},
                          Case{"\\\\server\\share\\x.foslib", "UNC"},
                          Case{"//server/share/x.foslib", "absolute"},
                          Case{"\\lib\\main.foslib", "absolute"},
                          Case{"lib\\..\\..\\outside.foslib", "backslash"},
                          Case{"lib/main.foslib:hidden", "alternate data streams"},
                          Case{"lib/main.foslib::$DATA", "alternate data streams"},
                          Case{"lib./main.foslib", "ends with a dot or a space"},
                          Case{"lib/main.foslib.", "ends with a dot or a space"},
                          Case{"lib/main.foslib ", "ends with a dot or a space"},
                          Case{"CON", "reserved Windows device name"},
                          Case{"lib/nul.foslib", "reserved Windows device name"},
                          Case{"Com1.foslib", "reserved Windows device name"},
                          Case{"lpt9/x.foslib", "reserved Windows device name"},
                          Case{"aux .foslib", "reserved Windows device name"},
                          Case{"CONOUT$.foslib", "reserved Windows device name"},
                          Case{"COM\xC2\xB9.foslib", "reserved Windows device name"},
                          Case{"lib/ma?n.foslib", "not allowed in Windows file names"},
                          Case{"lib/*.foslib", "not allowed in Windows file names"},
                          Case{"lib/a|b.foslib", "not allowed in Windows file names"}}) {
        auto r = resolveInside(dir, c.path);
        ASSERT_FALSE(r.ok()) << c.path;
        EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument) << c.path;
        EXPECT_THAT(r.error().message, HasSubstr(c.reason)) << c.path;
    }
    // Look-alikes that are ordinary names stay allowed.
    for (const char* ok : {"lib/main.foslib", "console.foslib", "com10.foslib", "nullable/x.foslib", "lib/.hidden.foslib"})
        EXPECT_EQ(checkRelativePath(ok), "") << ok;
    EXPECT_TRUE(resolveInside(dir, "lib/main.foslib").ok());
}

TEST(Package, SymlinkEscapeInManifestMakesPackageInvalid) {
    TempDir tmp;
    // A directory link (on Windows possibly a junction, which needs no privilege) to an
    // outside directory that holds the library.
    const auto dir3 = tmp / "org.example.evil3";
    writeText(dir3 / "plugin.json", manifestJson("org.example.evil3", "1.0.0", {"lib/main.foslib"}).dump());
    writeLibrary(tmp / "secret3/main.foslib", namespacedLibrary("org.example.evil3"));
    if (const auto why = tryCreateLink(tmp / "secret3", dir3 / "lib", true); !why.empty()) GTEST_SKIP() << why;
    auto i3 = inspectPackage(dir3);
    EXPECT_FALSE(i3.ok());
    EXPECT_TRUE(hasCode(i3.diagnostics, "plugins.package.bad_path")) << allMessages(i3.diagnostics);

    // A file link to an outside library.
    writeLibrary(tmp / "secret/outside.foslib", namespacedLibrary("org.example.evil"));
    const auto dir = tmp / "org.example.evil";
    writeText(dir / "plugin.json", manifestJson("org.example.evil", "1.0.0", {"lib/main.foslib"}).dump());
    fs::create_directories(dir / "lib");
    if (const auto why = tryCreateLink(tmp / "secret/outside.foslib", dir / "lib/main.foslib", false); !why.empty())
        GTEST_SKIP() << why;
    auto inspection = inspectPackage(dir);
    EXPECT_FALSE(inspection.ok());
    EXPECT_TRUE(hasCode(inspection.diagnostics, "plugins.package.bad_path"));
    EXPECT_THAT(allMessages(inspection.diagnostics), HasSubstr("/contributes/libraries/0: "));

    // plugin.json itself as a symlink to a file outside.
    const auto dir2 = tmp / "org.example.evil2";
    fs::create_directories(dir2);
    writeText(tmp / "secret/plugin.json", manifestJson("org.example.evil2").dump());
    ASSERT_EQ(tryCreateLink(tmp / "secret/plugin.json", dir2 / "plugin.json", false), "");
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
