// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/formats/atomic_file.hpp"
#include "fos/formats/container.hpp"
#include "fos/formats/sha256.hpp"
#include "test_support.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace fos::formats;
using fos::core::ErrorCode;
using fos::formats::test::TempDir;
using fos::formats::test::readText;
using fos::formats::test::writeText;
using ::testing::HasSubstr;

namespace {

FormatSpec projectSpec(int version = 1) {
    FormatSpec spec;
    spec.kind = "furnitureos-project";
    spec.currentVersion = version;
    return spec;
}

Document sampleDocument() {
    Document doc;
    doc.format = "furnitureos-project";
    doc.formatVersion = 1;
    doc.app.version = "0.1.0";
    doc.savedAt = "2026-01-31T12:00:00Z";
    doc.payload = Json::parse(R"({"name": "Kitchen", "items": [1, 2.5, "x"], "nested": {"k": null}})");
    return doc;
}

std::string sampleText() { return serializeDocument(sampleDocument()).value(); }

} // namespace

TEST(Container, CanonicalDumpSortsKeysAndIsCompact) {
    const Json payload = Json::parse("{\"b\": 1, \"a\": {\"z\": true, \"y\": \"\xC3\xA9\"}}");
    // Keys sorted at every level, no whitespace, UTF-8 kept as is.
    EXPECT_EQ(canonicalDump(payload).value(), "{\"a\":{\"y\":\"\xC3\xA9\",\"z\":true},\"b\":1}");
    // printf '{"a":{"y":"é","z":true},"b":1}' | sha256sum
    EXPECT_EQ(payloadChecksum(payload).value(), "08b7ee28ddf560ad09163633cc6eebaf23534cfd40df64bfd69886ee761931b0");
}

TEST(Container, ChecksumDoesNotDependOnKeyOrder) {
    const Json a = Json::parse(R"({"x": 1, "y": {"p": [1, 2], "q": "s"}})");
    const Json b = Json::parse(R"({"y": {"q": "s", "p": [1, 2]}, "x": 1})");
    EXPECT_EQ(payloadChecksum(a).value(), payloadChecksum(b).value());
    // Array order does matter.
    const Json c = Json::parse(R"({"x": 1, "y": {"p": [2, 1], "q": "s"}})");
    EXPECT_NE(payloadChecksum(a).value(), payloadChecksum(c).value());
}

TEST(Container, FormatIso8601Utc) {
    using namespace std::chrono;
    EXPECT_EQ(formatIso8601Utc(system_clock::time_point{}), "1970-01-01T00:00:00Z");
    // 10957 days to 2000-01-01, + 31 (Jan) + 28 = 11016 days -> 2000-02-29 (leap day); * 86400 = 951782400.
    EXPECT_EQ(formatIso8601Utc(system_clock::time_point{seconds{951782400}}), "2000-02-29T00:00:00Z");
    // Well-known timestamp 1700000000 = 2023-11-14 22:13:20 UTC.
    EXPECT_EQ(formatIso8601Utc(system_clock::time_point{seconds{1700000000}} + milliseconds{999}),
              "2023-11-14T22:13:20Z");
}

TEST(Container, SerializedEnvelopeHasTheDocumentedShape) {
    const std::string text = sampleText();
    const Json root = Json::parse(text);
    std::vector<std::string> keys;
    for (const auto& [key, value] : root.items()) keys.push_back(key);
    EXPECT_EQ(keys, (std::vector<std::string>{"format", "formatVersion", "app", "savedAt", "checksum", "payload"}));
    EXPECT_EQ(root["format"], "furnitureos-project");
    EXPECT_EQ(root["formatVersion"], 1);
    EXPECT_EQ(root["app"]["name"], "FurnitureOS");
    EXPECT_EQ(root["app"]["version"], "0.1.0");
    EXPECT_EQ(root["savedAt"], "2026-01-31T12:00:00Z");
    EXPECT_EQ(root["checksum"]["algorithm"], "sha256");
    // Checksum = SHA-256 of the canonical payload text, written out here by hand.
    EXPECT_EQ(root["checksum"]["value"].get<std::string>(),
              sha256Hex(R"({"items":[1,2.5,"x"],"name":"Kitchen","nested":{"k":null}})"));
    EXPECT_EQ(text.back(), '\n');
    // Deterministic: same input, same bytes.
    EXPECT_EQ(text, sampleText());
}

TEST(Container, RoundTripThroughText) {
    auto loaded = parseDocument(sampleText(), projectSpec());
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    const Document& doc = loaded.value().document;
    EXPECT_EQ(doc.format, "furnitureos-project");
    EXPECT_EQ(doc.formatVersion, 1);
    EXPECT_EQ(loaded.value().fileVersion, 1);
    EXPECT_EQ(doc.app.name, "FurnitureOS");
    EXPECT_EQ(doc.app.version, "0.1.0");
    EXPECT_EQ(doc.savedAt, "2026-01-31T12:00:00Z");
    EXPECT_EQ(doc.payload, sampleDocument().payload);
    EXPECT_TRUE(loaded.value().migrations.empty());
    EXPECT_TRUE(loaded.value().diagnostics.empty());
    // Saving the loaded document reproduces the file byte for byte.
    EXPECT_EQ(serializeDocument(doc).value(), sampleText());
}

TEST(Container, SerializeRejectsInvalidDocuments) {
    Document doc = sampleDocument();
    doc.format.clear();
    EXPECT_EQ(serializeDocument(doc).error().code, ErrorCode::InvalidArgument);
    doc = sampleDocument();
    doc.formatVersion = 0;
    EXPECT_THAT(serializeDocument(doc).error().message, HasSubstr("formatVersion 0"));
    doc = sampleDocument();
    doc.payload = Json::array({1, 2});
    EXPECT_THAT(serializeDocument(doc).error().message, HasSubstr("must be a JSON object, got an array"));
    doc = sampleDocument();
    doc.payload["bad"] = std::string("\xFF\xFE");
    auto r = serializeDocument(doc);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::InvalidArgument);
    EXPECT_THAT(r.error().message, HasSubstr("not valid UTF-8"));
}

// ---------------------------------------------------------------- load errors

TEST(ContainerErrors, MissingFileAndDirectory) {
    TempDir dir;
    auto missing = loadDocument(dir / "nope.fosp", projectSpec());
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().code, ErrorCode::IoError);
    EXPECT_THAT(missing.error().message, HasSubstr("nope.fosp"));
    EXPECT_THAT(missing.error().message, HasSubstr("does not exist"));

    auto directory = loadDocument(dir.path(), projectSpec());
    ASSERT_FALSE(directory.ok());
    EXPECT_EQ(directory.error().code, ErrorCode::IoError);
    EXPECT_THAT(directory.error().message, HasSubstr("is a directory"));
}

TEST(ContainerErrors, NotJsonReportsLineAndColumn) {
    // Line 2 is `  "format": oops`: two spaces (columns 1-2), "format" in quotes (3-10),
    // ':' (11), space (12), so the offending 'o' is at line 2, column 13.
    auto r = parseDocument("{\n  \"format\": oops\n}", projectSpec(), {}, "broken.fosp");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::ParseError);
    EXPECT_THAT(r.error().message, HasSubstr("'broken.fosp' is not valid JSON (line 2, column 13)"));
}

TEST(ContainerErrors, EmptyAndWhitespaceOnlyFiles) {
    TempDir dir;
    writeText(dir / "empty.fosp", "");
    auto r = loadDocument(dir / "empty.fosp", projectSpec());
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::ParseError);
    EXPECT_THAT(r.error().message, HasSubstr("'empty.fosp' is empty"));
    EXPECT_EQ(parseDocument(" \n\t ", projectSpec()).error().code, ErrorCode::ParseError);
}

TEST(ContainerErrors, BinaryGarbageAndExcessiveNesting) {
    EXPECT_EQ(parseDocument(std::string("\x00\x01\xFF\xFEPK\x03\x04", 8), projectSpec()).error().code,
              ErrorCode::ParseError);
    // 100 000 nested arrays must be refused cleanly, not overflow the stack.
    auto deep = parseDocument(std::string(100000, '['), projectSpec());
    ASSERT_FALSE(deep.ok());
    EXPECT_EQ(deep.error().code, ErrorCode::ParseError);
    EXPECT_THAT(deep.error().message, HasSubstr("deeper than 128 levels"));
    // Brackets inside strings do not count as nesting.
    Document doc = sampleDocument();
    doc.payload["text"] = std::string(1000, '[');
    EXPECT_TRUE(parseDocument(serializeDocument(doc).value(), projectSpec()).ok());
}

TEST(ContainerErrors, TopLevelNotAnObject) {
    auto r = parseDocument("[1, 2, 3]", projectSpec(), {}, "list.fosp");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::CorruptData);
    EXPECT_THAT(r.error().message, HasSubstr("top-level value is an array"));
}

TEST(ContainerErrors, MissingOrWrongFormatKind) {
    Json root = Json::parse(sampleText());
    root.erase("format");
    auto missing = parseDocument(root.dump(), projectSpec());
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().code, ErrorCode::CorruptData);
    EXPECT_THAT(missing.error().message, HasSubstr("\"format\" key is missing"));

    root["format"] = 42;
    EXPECT_EQ(parseDocument(root.dump(), projectSpec()).error().code, ErrorCode::CorruptData);

    root["format"] = "furnitureos-library";
    auto wrong = parseDocument(root.dump(), projectSpec(), {}, "lib.foslib");
    ASSERT_FALSE(wrong.ok());
    EXPECT_EQ(wrong.error().code, ErrorCode::Unsupported);
    EXPECT_THAT(wrong.error().message,
                HasSubstr("'lib.foslib' is a \"furnitureos-library\" file, but a \"furnitureos-project\" file is expected"));
}

TEST(ContainerErrors, NewerVersionIsRefusedWithoutTouchingThePayload) {
    Document doc = sampleDocument();
    doc.formatVersion = 7;
    doc.app.version = "3.2.0";
    bool migrationRan = false;
    FormatSpec spec = projectSpec(3);
    for (int v = 1; v < 3; ++v)
        ASSERT_TRUE(spec.migrations.add(v, [&](Json&) -> fos::core::Result<void> {
            migrationRan = true;
            return {};
        }).ok());
    auto r = parseDocument(serializeDocument(doc).value(), spec, {}, "future.fosp");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::UnsupportedVersion);
    EXPECT_THAT(r.error().message, HasSubstr("format version 7"));
    EXPECT_THAT(r.error().message, HasSubstr("supports only up to version 3"));
    EXPECT_THAT(r.error().message, HasSubstr("saved by FurnitureOS 3.2.0"));
    EXPECT_THAT(r.error().message, HasSubstr("Nothing was loaded"));
    EXPECT_FALSE(migrationRan);

    // Even the recovery flag and a broken checksum do not let a newer file through.
    Json root = Json::parse(serializeDocument(doc).value());
    root["checksum"]["value"] = "00";
    root.erase("app");
    LoadOptions lenient;
    lenient.allowChecksumMismatch = true;
    auto r2 = parseDocument(root.dump(), spec, lenient);
    ASSERT_FALSE(r2.ok());
    EXPECT_EQ(r2.error().code, ErrorCode::UnsupportedVersion);
    EXPECT_THAT(r2.error().message, HasSubstr("not recorded in the file"));
}

TEST(ContainerErrors, InvalidFormatVersionValues) {
    Json root = Json::parse(sampleText());
    for (const char* bad : {"0", "-3", "1.5", "\"1\"", "null"}) {
        root["formatVersion"] = Json::parse(bad);
        auto r = parseDocument(root.dump(), projectSpec());
        ASSERT_FALSE(r.ok()) << bad;
        EXPECT_EQ(r.error().code, ErrorCode::CorruptData) << bad;
    }
    root.erase("formatVersion");
    EXPECT_THAT(parseDocument(root.dump(), projectSpec()).error().message, HasSubstr("no \"formatVersion\""));
    // Absurdly large versions are "newer", not a crash or overflow.
    root["formatVersion"] = Json::parse("18446744073709551615");
    EXPECT_EQ(parseDocument(root.dump(), projectSpec()).error().code, ErrorCode::UnsupportedVersion);
}

TEST(ContainerErrors, MissingOrIllTypedPayload) {
    Json root = Json::parse(sampleText());
    root["payload"] = Json::array();
    EXPECT_THAT(parseDocument(root.dump(), projectSpec()).error().message, HasSubstr("\"payload\" that is an array"));
    root.erase("payload");
    auto r = parseDocument(root.dump(), projectSpec(), {}, "half.fosp");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::CorruptData);
    EXPECT_THAT(r.error().message, HasSubstr("'half.fosp' has no \"payload\""));
}

TEST(ContainerErrors, ChecksumMismatchAndRecoveryFlag) {
    Json root = Json::parse(sampleText());
    root["payload"]["name"] = "Kitchen (edited by hand)";
    const std::string tampered = root.dump(2);

    auto strict = parseDocument(tampered, projectSpec(), {}, "edited.fosp");
    ASSERT_FALSE(strict.ok());
    EXPECT_EQ(strict.error().code, ErrorCode::CorruptData);
    EXPECT_THAT(strict.error().message, HasSubstr("failed its integrity check"));
    EXPECT_THAT(strict.error().message, HasSubstr(root["checksum"]["value"].get<std::string>()));

    LoadOptions lenient;
    lenient.allowChecksumMismatch = true;
    auto recovered = parseDocument(tampered, projectSpec(), lenient, "edited.fosp");
    ASSERT_TRUE(recovered.ok()) << recovered.error().message;
    EXPECT_EQ(recovered.value().document.payload["name"], "Kitchen (edited by hand)");
    ASSERT_EQ(recovered.value().diagnostics.size(), 1u);
    EXPECT_EQ(recovered.value().diagnostics[0].severity, fos::core::Severity::Warning);
    EXPECT_EQ(recovered.value().diagnostics[0].code, "formats.container.checksum_mismatch");
}

TEST(ContainerErrors, MissingMalformedOrUnknownChecksum) {
    Json root = Json::parse(sampleText());
    LoadOptions lenient;
    lenient.allowChecksumMismatch = true;

    Json unknownAlgorithm = root;
    unknownAlgorithm["checksum"]["algorithm"] = "md5";
    auto r = parseDocument(unknownAlgorithm.dump(), projectSpec());
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::CorruptData);
    EXPECT_THAT(r.error().message, HasSubstr("unknown checksum algorithm \"md5\""));

    Json malformed = root;
    malformed["checksum"] = "abc";
    EXPECT_THAT(parseDocument(malformed.dump(), projectSpec()).error().message, HasSubstr("malformed \"checksum\""));

    root.erase("checksum");
    auto missing = parseDocument(root.dump(), projectSpec());
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().code, ErrorCode::CorruptData);
    auto accepted = parseDocument(root.dump(), projectSpec(), lenient);
    ASSERT_TRUE(accepted.ok());
    EXPECT_EQ(accepted.value().diagnostics.at(0).code, "formats.container.checksum_missing");
}

TEST(ContainerErrors, TruncatedFilesNeverLoadAndNeverCrash) {
    const std::string text = sampleText();
    ASSERT_GT(text.size(), 200u);
    LoadOptions lenient;
    lenient.allowChecksumMismatch = true;
    // Every proper prefix that lost at least the final '}' must be rejected, in strict
    // and in recovery mode. (Only the trailing newline may be lost harmlessly.)
    for (std::size_t length = 0; length + 1 < text.size(); ++length) {
        const std::string_view prefix(text.data(), length);
        auto strict = parseDocument(prefix, projectSpec());
        ASSERT_FALSE(strict.ok()) << "prefix of " << length << " bytes loaded";
        EXPECT_TRUE(strict.error().code == ErrorCode::ParseError || strict.error().code == ErrorCode::CorruptData);
        EXPECT_FALSE(strict.error().message.empty());
        EXPECT_FALSE(parseDocument(prefix, projectSpec(), lenient).ok()) << length;
    }
    EXPECT_TRUE(parseDocument(std::string_view(text.data(), text.size() - 1), projectSpec()).ok());
}

TEST(ContainerErrors, TruncatedAndDamagedFilesOnDisk) {
    TempDir dir;
    const std::string text = sampleText();
    for (const std::size_t length : {std::size_t{1}, std::size_t{17}, text.size() / 3, text.size() / 2, text.size() - 5}) {
        writeText(dir / "cut.fosp", text.substr(0, length));
        auto r = loadDocument(dir / "cut.fosp", projectSpec());
        ASSERT_FALSE(r.ok()) << length;
        EXPECT_THAT(r.error().message, HasSubstr("cut.fosp")) << length;
    }
    // A single flipped character inside the payload is caught by the checksum.
    std::string flipped = text;
    const auto pos = flipped.find("Kitchen");
    ASSERT_NE(pos, std::string::npos);
    flipped[pos] = 'M';
    writeText(dir / "flip.fosp", flipped);
    EXPECT_EQ(loadDocument(dir / "flip.fosp", projectSpec()).error().code, ErrorCode::CorruptData);
}

// ---------------------------------------------------------------- migrations

namespace {
/// v1 {"title"} -> v2 {"name"} -> v3 {"name", "units": "mm"}.
FormatSpec migratingSpec(std::vector<int>* calls = nullptr) {
    FormatSpec spec = projectSpec(3);
    EXPECT_TRUE(spec.migrations.add(1, [calls](Json& p) -> fos::core::Result<void> {
        if (calls) calls->push_back(1);
        if (!p.contains("title")) return fos::core::makeError(ErrorCode::CorruptData, "v1 payload has no \"title\".");
        p["name"] = p["title"];
        p.erase("title");
        return {};
    }, "rename title to name").ok());
    EXPECT_TRUE(spec.migrations.add(2, [calls](Json& p) -> fos::core::Result<void> {
        if (calls) calls->push_back(2);
        p["units"] = "mm";
        return {};
    }, "add units").ok());
    return spec;
}

std::string documentText(int version, const char* payload) {
    Document doc = sampleDocument();
    doc.formatVersion = version;
    doc.payload = Json::parse(payload);
    return serializeDocument(doc).value();
}
} // namespace

TEST(Migrations, ChainFromV1ToV3RunsInOrderAndIsReported) {
    std::vector<int> calls;
    auto loaded = parseDocument(documentText(1, R"({"title": "Wardrobe", "future": {"keep": [1, 2]}})"),
                                migratingSpec(&calls));
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    EXPECT_EQ(calls, (std::vector<int>{1, 2}));
    EXPECT_EQ(loaded.value().fileVersion, 1);
    EXPECT_EQ(loaded.value().document.formatVersion, 3);
    ASSERT_EQ(loaded.value().migrations.size(), 2u);
    EXPECT_EQ(loaded.value().migrations[0].fromVersion, 1);
    EXPECT_EQ(loaded.value().migrations[0].toVersion, 2);
    EXPECT_EQ(loaded.value().migrations[0].description, "rename title to name");
    EXPECT_EQ(loaded.value().migrations[1].fromVersion, 2);
    EXPECT_EQ(loaded.value().migrations[1].toVersion, 3);
    EXPECT_EQ(loaded.value().document.payload,
              Json::parse(R"({"future": {"keep": [1, 2]}, "name": "Wardrobe", "units": "mm"})"));
}

TEST(Migrations, V2FileRunsOnlyTheLastStepAndV3RunsNone) {
    std::vector<int> calls;
    auto v2 = parseDocument(documentText(2, R"({"name": "A"})"), migratingSpec(&calls));
    ASSERT_TRUE(v2.ok());
    EXPECT_EQ(calls, (std::vector<int>{2}));
    EXPECT_EQ(v2.value().document.payload["units"], "mm");
    calls.clear();
    auto v3 = parseDocument(documentText(3, R"({"name": "A", "units": "mm"})"), migratingSpec(&calls));
    ASSERT_TRUE(v3.ok());
    EXPECT_TRUE(calls.empty());
    EXPECT_TRUE(v3.value().migrations.empty());
}

TEST(Migrations, MissingStepIsAnErrorAndNothingRuns) {
    std::vector<int> calls;
    FormatSpec spec = projectSpec(3);
    ASSERT_TRUE(spec.migrations.add(1, [&](Json&) -> fos::core::Result<void> {
        calls.push_back(1);
        return {};
    }).ok()); // step 2 -> 3 is missing
    auto r = parseDocument(documentText(1, R"({"title": "x"})"), spec, {}, "old.fosp");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::UnsupportedVersion);
    EXPECT_THAT(r.error().message, HasSubstr("'old.fosp' (format version 1) could not be upgraded"));
    EXPECT_THAT(r.error().message, HasSubstr("No migration from format version 2 to 3"));
    EXPECT_TRUE(calls.empty());
}

TEST(Migrations, FailingOrThrowingStepLeavesThePayloadUntouched) {
    const MigrationRegistry registry = migratingSpec().migrations;
    Json payload = Json::parse(R"({"nottitle": 1})");
    const Json before = payload;
    auto failed = registry.run(payload, 1, 3);
    ASSERT_FALSE(failed.ok());
    EXPECT_THAT(failed.error().message, HasSubstr("Migration 1 -> 2 failed: v1 payload has no \"title\"."));
    EXPECT_EQ(payload, before);

    MigrationRegistry throwing;
    ASSERT_TRUE(throwing.add(1, [](Json& p) -> fos::core::Result<void> {
        p["partly"] = "done";
        (void)p.at("missing").get<int>(); // throws nlohmann::json::out_of_range
        return {};
    }).ok());
    auto thrown = throwing.run(payload, 1, 2);
    ASSERT_FALSE(thrown.ok());
    EXPECT_THAT(thrown.error().message, HasSubstr("Migration 1 -> 2 failed"));
    EXPECT_EQ(payload, before);
}

TEST(Migrations, RegistryRejectsBadRegistrations) {
    MigrationRegistry registry;
    const MigrationStep noop = [](Json&) -> fos::core::Result<void> { return {}; };
    EXPECT_EQ(registry.add(0, noop).error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(registry.add(1, nullptr).error().code, ErrorCode::InvalidArgument);
    EXPECT_TRUE(registry.add(1, noop).ok());
    EXPECT_EQ(registry.add(1, noop).error().code, ErrorCode::AlreadyExists);
    EXPECT_TRUE(registry.has(1));
    EXPECT_FALSE(registry.has(2));
    Json payload = Json::object();
    EXPECT_EQ(registry.run(payload, 3, 1).error().code, ErrorCode::InvalidArgument);
}

// ---------------------------------------------------------------- unknown keys

TEST(Container, UnknownKeysSurviveLoadSaveLoad) {
    // A file written by a (hypothetical) later patch release: unknown keys at the
    // envelope level and at several depths of the payload, in non-alphabetical order.
    Document doc = sampleDocument();
    doc.payload = Json::parse(R"({
        "zeta": {"deep": [{"x": 1.25, "unknownFlag": true}, null]},
        "name": "Kitchen",
        "futureFeature": {"mode": "auto", "values": [3, 2, 1]}
    })");
    doc.extra = Json::parse(R"({"signature": {"by": "plugin-x", "data": [1, 2, 3]}, "comment": "hello"})");
    const std::string original = serializeDocument(doc).value();

    TempDir dir;
    writeText(dir / "a.fosp", original);
    auto first = loadDocument(dir / "a.fosp", projectSpec());
    ASSERT_TRUE(first.ok()) << first.error().message;
    EXPECT_EQ(first.value().document.extra["signature"]["by"], "plugin-x");

    ASSERT_TRUE(saveDocument(dir / "b.fosp", first.value().document).ok());
    auto second = loadDocument(dir / "b.fosp", projectSpec());
    ASSERT_TRUE(second.ok()) << second.error().message;
    EXPECT_EQ(second.value().document.payload, doc.payload);
    EXPECT_EQ(second.value().document.extra, doc.extra);
    // Key order is preserved as well, so the re-saved file is byte-identical.
    EXPECT_EQ(readText(dir / "b.fosp"), original);
}
