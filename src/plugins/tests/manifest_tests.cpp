// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/plugins/manifest.hpp"
#include "plugin_test_support.hpp"

#include <functional>

using namespace fos;
using namespace fos::plugins;
using namespace fos::plugins::test;

namespace {

ManifestValidation parse(const Json& j) { return parseManifest(j.dump()); }

/// Message of the first error, or "" when there is none.
std::string firstError(const ManifestValidation& v) {
    for (const auto& d : v.diagnostics)
        if (d.severity == core::Severity::Error) return d.message;
    return {};
}

} // namespace

TEST(Manifest, ValidManifestIsParsedCompletely) {
    Json j = manifestJson("org.example.oak-boards", "2.1.0-beta.1", {"a.foslib", "sub/dir/b.foslib"});
    j["homepage"] = "https://example.org/plugins/oak";
    j["compatibility"]["maxAppVersion"] = "1.0.0";
    j["license"] = "MIT OR Apache-2.0";
    j["localizedNames"] = Json{{"uk", "Дубові плити"}, {"en-GB", "Oak boards"}};
    const auto v = parse(j);
    ASSERT_TRUE(v.ok()) << allMessages(v.diagnostics);
    EXPECT_TRUE(v.diagnostics.empty()) << allMessages(v.diagnostics);
    const auto& m = v.manifest;
    EXPECT_EQ(m.manifestVersion, 1);
    EXPECT_EQ(m.id, "org.example.oak-boards");
    EXPECT_EQ(m.version.toString(), "2.1.0-beta.1");
    EXPECT_EQ(m.versionText, "2.1.0-beta.1");
    EXPECT_EQ(m.license, "MIT OR Apache-2.0");
    EXPECT_EQ(m.homepage.value(), "https://example.org/plugins/oak");
    EXPECT_EQ(m.compatibility.minAppVersion.toString(), "0.1.0");
    EXPECT_EQ(m.compatibility.maxAppVersion->toString(), "1.0.0");
    EXPECT_EQ(m.permissions, std::vector<std::string>{"data"});
    EXPECT_EQ(m.libraries, (std::vector<std::string>{"a.foslib", "sub/dir/b.foslib"}));
    EXPECT_EQ(m.localizedNames.at("uk"), "Дубові плити");
}

struct MatrixCase {
    const char* name;
    std::function<void(Json&)> mutate;
    const char* pathPrefix; ///< The error message must start with this JSON pointer.
    const char* code;
};

TEST(Manifest, ValidationMatrixReportsJsonPaths) {
    const std::vector<MatrixCase> cases = {
        {"missing id", [](Json& j) { j.erase("id"); }, "/id: ", "plugins.manifest.missing_key"},
        {"id upper case", [](Json& j) { j["id"] = "Org.Example.X"; }, "/id: ", "plugins.manifest.bad_id"},
        {"id one segment", [](Json& j) { j["id"] = "example"; }, "/id: ", "plugins.manifest.bad_id"},
        {"id with colon", [](Json& j) { j["id"] = "org.ex:x"; }, "/id: ", "plugins.manifest.bad_id"},
        {"id not a string", [](Json& j) { j["id"] = 7; }, "/id: ", "plugins.manifest.wrong_type"},
        {"missing manifestVersion", [](Json& j) { j.erase("manifestVersion"); }, "/manifestVersion: ", "plugins.manifest.missing_key"},
        {"manifestVersion 2", [](Json& j) { j["manifestVersion"] = 2; }, "/manifestVersion: ", "plugins.manifest.unsupported_version"},
        {"manifestVersion 0", [](Json& j) { j["manifestVersion"] = 0; }, "/manifestVersion: ", "plugins.manifest.bad_value"},
        {"manifestVersion string", [](Json& j) { j["manifestVersion"] = "1"; }, "/manifestVersion: ", "plugins.manifest.wrong_type"},
        {"empty name", [](Json& j) { j["name"] = "  "; }, "/name: ", "plugins.manifest.empty"},
        {"missing version", [](Json& j) { j.erase("version"); }, "/version: ", "plugins.manifest.missing_key"},
        {"bad version", [](Json& j) { j["version"] = "1.0"; }, "/version: ", "plugins.manifest.bad_version"},
        {"missing description", [](Json& j) { j.erase("description"); }, "/description: ", "plugins.manifest.missing_key"},
        {"missing author", [](Json& j) { j.erase("author"); }, "/author: ", "plugins.manifest.missing_key"},
        {"empty license", [](Json& j) { j["license"] = ""; }, "/license: ", "plugins.manifest.bad_license"},
        {"license with spaces", [](Json& j) { j["license"] = "Public Domain"; }, "/license: ", "plugins.manifest.bad_license"},
        {"license parentheses", [](Json& j) { j["license"] = "(MIT OR GPL-2.0)"; }, "/license: ", "plugins.manifest.bad_license"},
        {"license trailing op", [](Json& j) { j["license"] = "MIT OR"; }, "/license: ", "plugins.manifest.bad_license"},
        {"http homepage", [](Json& j) { j["homepage"] = "http://example.org"; }, "/homepage: ", "plugins.manifest.bad_homepage"},
        {"javascript homepage", [](Json& j) { j["homepage"] = "javascript:alert(1)"; }, "/homepage: ", "plugins.manifest.bad_homepage"},
        {"homepage no host", [](Json& j) { j["homepage"] = "https:///x"; }, "/homepage: ", "plugins.manifest.bad_homepage"},
        {"missing compatibility", [](Json& j) { j.erase("compatibility"); }, "/compatibility: ", "plugins.manifest.missing_key"},
        {"missing minAppVersion", [](Json& j) { j["compatibility"] = Json::object(); }, "/compatibility/minAppVersion: ", "plugins.manifest.missing_key"},
        {"bad maxAppVersion", [](Json& j) { j["compatibility"]["maxAppVersion"] = "x"; }, "/compatibility/maxAppVersion: ", "plugins.manifest.bad_version"},
        {"inverted range", [](Json& j) { j["compatibility"] = Json{{"minAppVersion", "2.0.0"}, {"maxAppVersion", "1.0.0"}}; }, "/compatibility/maxAppVersion: ", "plugins.manifest.bad_range"},
        {"missing permissions", [](Json& j) { j.erase("permissions"); }, "/permissions: ", "plugins.manifest.missing_key"},
        {"empty permissions", [](Json& j) { j["permissions"] = Json::array(); }, "/permissions: ", "plugins.manifest.empty"},
        {"permission not string", [](Json& j) { j["permissions"] = Json::array({"data", 3}); }, "/permissions/1: ", "plugins.manifest.wrong_type"},
        {"duplicate permission", [](Json& j) { j["permissions"] = Json::array({"data", "data"}); }, "/permissions/1: ", "plugins.manifest.duplicate"},
        {"network permission", [](Json& j) { j["permissions"] = Json::array({"data", "network"}); }, "/permissions/1: ", "plugins.manifest.unsupported_permission"},
        {"missing contributes", [](Json& j) { j.erase("contributes"); }, "/contributes: ", "plugins.manifest.missing_key"},
        {"libraries not array", [](Json& j) { j["contributes"]["libraries"] = "a.foslib"; }, "/contributes/libraries: ", "plugins.manifest.wrong_type"},
        {"library not string", [](Json& j) { j["contributes"]["libraries"] = Json::array({1}); }, "/contributes/libraries/0: ", "plugins.manifest.wrong_type"},
        {"absolute library", [](Json& j) { j["contributes"]["libraries"] = Json::array({"/etc/x.foslib"}); }, "/contributes/libraries/0: ", "plugins.manifest.bad_path"},
        {"traversal library", [](Json& j) { j["contributes"]["libraries"] = Json::array({"ok.foslib", "../x.foslib"}); }, "/contributes/libraries/1: ", "plugins.manifest.bad_path"},
        {"wrong extension", [](Json& j) { j["contributes"]["libraries"] = Json::array({"lib.json"}); }, "/contributes/libraries/0: ", "plugins.manifest.bad_path"},
        {"duplicate library", [](Json& j) { j["contributes"]["libraries"] = Json::array({"a.foslib", "a.foslib"}); }, "/contributes/libraries/1: ", "plugins.manifest.duplicate"},
        {"bad language tag", [](Json& j) { j["localizedNames"] = Json{{"Ukrainian", "x"}}; }, "/localizedNames/Ukrainian: ", "plugins.manifest.bad_language"},
        {"empty localized name", [](Json& j) { j["localizedNames"] = Json{{"uk", ""}}; }, "/localizedNames/uk: ", "plugins.manifest.empty"},
    };
    for (const auto& c : cases) {
        Json j = manifestJson("org.example.matrix");
        c.mutate(j);
        const auto v = parse(j);
        EXPECT_FALSE(v.ok()) << c.name;
        EXPECT_TRUE(hasCode(v.diagnostics, c.code)) << c.name << "\n" << allMessages(v.diagnostics);
        EXPECT_EQ(firstError(v).rfind(c.pathPrefix, 0), 0u) << c.name << ": " << firstError(v);
    }
}

TEST(Manifest, UnknownKeysAndUnsupportedContributionsAreWarnings) {
    Json j = manifestJson("org.example.extra");
    j["futureField"] = true;
    j["compatibility"]["platform"] = "any";
    j["contributes"]["templates"] = Json::array({"t/cabinet.json"});
    j["contributes"]["postprocessors"] = Json::array({"pp/router.lua"});
    j["contributes"]["widgets"] = Json::array();
    const auto v = parse(j);
    ASSERT_TRUE(v.ok()) << allMessages(v.diagnostics);
    ASSERT_EQ(v.diagnostics.size(), 5u) << allMessages(v.diagnostics);
    for (const auto& d : v.diagnostics) EXPECT_EQ(d.severity, core::Severity::Warning);
    EXPECT_EQ(v.diagnostics[0].message.rfind("/compatibility/platform: unknown key", 0), 0u);
    EXPECT_EQ(v.diagnostics[1].message,
              "/contributes/templates: contribution kind \"templates\" is not supported by this version of FurnitureOS and is ignored.");
    EXPECT_EQ(v.diagnostics[1].entityId, "org.example.extra");
    EXPECT_EQ(v.diagnostics[2].code, "plugins.manifest.unsupported_contribution");
    EXPECT_EQ(v.diagnostics[3].message.rfind("/contributes/widgets: unknown contribution kind", 0), 0u);
    EXPECT_EQ(v.diagnostics[4].message.rfind("/futureField: unknown key", 0), 0u);
    EXPECT_EQ(v.manifest.ignoredContributions,
              (std::vector<ExtensionKind>{ExtensionKind::Templates, ExtensionKind::Postprocessors}));
    EXPECT_EQ(v.manifest.libraries.size(), 1u);
}

TEST(Manifest, ExtensionKinds) {
    EXPECT_TRUE(isSupported(ExtensionKind::Libraries));
    for (auto k : {ExtensionKind::Templates, ExtensionKind::Reports, ExtensionKind::Postprocessors, ExtensionKind::Importers}) {
        EXPECT_FALSE(isSupported(k));
        EXPECT_EQ(extensionKindFromKey(toString(k)), k);
    }
    EXPECT_FALSE(extensionKindFromKey("scripts").has_value());
}

TEST(Manifest, UnsupportedPermissionMessageIsClear) {
    Json j = manifestJson("org.example.net");
    j["permissions"] = Json::array({"data", "network"});
    const auto v = parse(j);
    EXPECT_EQ(firstError(v), "/permissions/1: permission \"network\" is not granted by this version of FurnitureOS; "
                             "only \"data\" (read-only data files) is defined (ADR-0007 phase 1). The plugin cannot be loaded.");
}

TEST(Manifest, PluginIdRules) {
    for (const char* good : {"org.example", "org.example.oak-boards", "com.a1.b2-c3.d", "io.x9"})
        EXPECT_EQ(checkPluginId(good), "") << good;
    for (const char* bad : {"", "ab", "example", "Org.example", "org..example", "org.example.", ".org.example",
                            "org.-x", "org.x-", "org.x--y", "org.1x", "org.ex ample", "org/example", "org.ex:x",
                            "org.ex_ample"})
        EXPECT_NE(checkPluginId(bad), "") << bad;
    EXPECT_NE(checkPluginId("org." + std::string(125, 'a')), ""); // 129 characters
    EXPECT_EQ(checkPluginId("org." + std::string(124, 'a')), ""); // 128 characters
}

TEST(Manifest, RelativePathRules) {
    for (const char* good : {"a.foslib", "lib/a.foslib", "x/y/z.foslib", "..a.foslib", "a..b.foslib"})
        EXPECT_EQ(checkRelativePath(good), "") << good;
    for (const char* bad : {"", "/a.foslib", "../a.foslib", "lib/../../a.foslib", "lib/..", "./a.foslib", "lib//a.foslib",
                            "lib/", "C:/a.foslib", "C:a.foslib", "lib\\a.foslib", "\\\\server\\a.foslib", "a\n.foslib"})
        EXPECT_NE(checkRelativePath(bad), "") << bad;
}

TEST(Manifest, SizeDepthAndSyntaxLimits) {
    PluginLimits limits;
    limits.maxManifestBytes = 100;
    auto big = parseManifest(std::string(101, ' '), limits);
    ASSERT_FALSE(big.ok());
    EXPECT_EQ(big.diagnostics[0].message, "/: the manifest is 101 bytes; the limit is 100 bytes.");

    std::string deep = std::string(40, '[') + std::string(40, ']');
    auto d = parseManifest(deep);
    ASSERT_FALSE(d.ok());
    EXPECT_EQ(d.diagnostics[0].code, "plugins.manifest.too_deep");
    EXPECT_NE(d.diagnostics[0].message.find("40 levels"), std::string::npos);
    // Brackets inside strings do not count.
    Json j = manifestJson("org.example.brackets");
    j["description"] = std::string(100, '[');
    EXPECT_TRUE(parse(j).ok());

    auto notJson = parseManifest("{\"id\": ");
    ASSERT_FALSE(notJson.ok());
    EXPECT_EQ(notJson.diagnostics[0].code, "plugins.manifest.parse_error");
    auto array = parseManifest("[]");
    EXPECT_EQ(array.diagnostics[0].message, "/: the manifest must be a JSON object, found an array.");
    auto invalidUtf8 = parseManifest("{\"id\": \"org.ex\xff\"}");
    EXPECT_FALSE(invalidUtf8.ok());

    Json many = manifestJson("org.example.many");
    many["contributes"]["libraries"] = Json::array();
    for (int i = 0; i < 33; ++i) many["contributes"]["libraries"].push_back("l" + std::to_string(i) + ".foslib");
    auto m = parse(many); // default limit 32
    EXPECT_TRUE(hasCode(m.diagnostics, "plugins.manifest.too_many")) << allMessages(m.diagnostics);
}

TEST(Manifest, CompatibilityRangeIsInclusive) {
    Compatibility c;
    c.minAppVersion = parseSemVer("0.2.0").value();
    c.maxAppVersion = parseSemVer("0.4.0").value();
    EXPECT_FALSE(isCompatible(c, parseSemVer("0.1.9").value()));
    EXPECT_FALSE(isCompatible(c, parseSemVer("0.2.0-dev").value())); // pre-release precedes 0.2.0
    EXPECT_TRUE(isCompatible(c, parseSemVer("0.2.0").value()));
    EXPECT_TRUE(isCompatible(c, parseSemVer("0.4.0").value()));
    EXPECT_FALSE(isCompatible(c, parseSemVer("0.4.1").value()));
    c.maxAppVersion.reset();
    EXPECT_TRUE(isCompatible(c, parseSemVer("99.0.0").value()));
}
