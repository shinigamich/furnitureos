// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/domain/fixtures.hpp"
#include "fos/formats/codec.hpp"
#include "fos/formats/library.hpp"
#include "model_equality.hpp"
#include "test_support.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace fos;
using namespace fos::formats;
using core::ErrorCode;
using formats::test::TempDir;
using formats::test::expectEqualLists;
using ::testing::HasSubstr;

namespace {

Library fixtureLibrary() { return libraryOf(domain::fixtures::basicCabinet()); }

domain::Material material(const char* id, const char* name, double thickness) {
    domain::Material m;
    m.id = domain::MaterialId(id);
    m.name = name;
    m.thickness = thickness;
    return m;
}

IdGenerator sequence(std::vector<std::string> ids) {
    auto next = std::make_shared<std::size_t>(0);
    return [ids = std::move(ids), next] { return *next < ids.size() ? ids[(*next)++] : std::string(); };
}

} // namespace

TEST(Library, ExportImportRoundTrip) {
    TempDir dir;
    const auto file = dir / "workshop.foslib";
    const Library original = fixtureLibrary();
    ASSERT_EQ(original.materials.size(), 2u);
    ASSERT_EQ(original.edgeBands.size(), 2u);
    ASSERT_EQ(original.hardware.size(), 1u);

    SaveOptions options;
    options.clock = [] { return std::chrono::system_clock::time_point{std::chrono::seconds{1700000000}}; };
    auto report = exportLibrary(file, original, AppInfo{"FurnitureOS", "0.1.0"}, options);
    ASSERT_TRUE(report.ok()) << report.error().message;

    // The file is a version-1 "furnitureos-library" container.
    const Json root = Json::parse(formats::test::readText(file));
    EXPECT_EQ(root["format"], "furnitureos-library");
    EXPECT_EQ(root["formatVersion"], 1);
    EXPECT_EQ(root["savedAt"], "2023-11-14T22:13:20Z");
    EXPECT_EQ(root["app"]["version"], "0.1.0");
    EXPECT_EQ(root["payload"]["materials"].size(), 2u);
    EXPECT_EQ(root["payload"]["edgeBands"].size(), 2u);
    EXPECT_EQ(root["payload"]["hardware"][0]["id"], "hw.shelf_pin_5");

    auto imported = importLibrary(file);
    ASSERT_TRUE(imported.ok()) << imported.error().message;
    expectEqualLists(original.materials, imported.value().materials, "materials");
    expectEqualLists(original.edgeBands, imported.value().edgeBands, "edgeBands");
    expectEqualLists(original.hardware, imported.value().hardware, "hardware");
    EXPECT_TRUE(imported.value().extra.empty());
}

TEST(Library, SpecAndConstants) {
    EXPECT_EQ(libraryFormatSpec().kind, "furnitureos-library");
    EXPECT_EQ(libraryFormatSpec().currentVersion, 1);
    EXPECT_EQ(kLibraryFileExtension, ".foslib");
}

TEST(Library, DuplicateIdsInsideOneFileAreAnError) {
    TempDir dir;
    // A hand-made (or foreign) file with a valid envelope whose payload repeats an id.
    Document doc;
    doc.format = std::string(kLibraryFormatKind);
    doc.payload = libraryToJson(fixtureLibrary());
    doc.payload["edgeBands"].push_back(doc.payload["edgeBands"][0]);
    ASSERT_TRUE(saveDocument(dir / "dup.foslib", doc).ok());
    auto imported = importLibrary(dir / "dup.foslib");
    ASSERT_FALSE(imported.ok());
    EXPECT_EQ(imported.error().code, ErrorCode::ParseError);
    EXPECT_THAT(imported.error().message,
                HasSubstr("'dup.foslib' is not a valid library: /payload/edgeBands/2/id: duplicate id \"eb.abs2\" "
                          "(already used by /payload/edgeBands/0)"));

    // Exporting such a library is refused before anything is written.
    Library broken = fixtureLibrary();
    broken.materials.push_back(broken.materials[0]);
    auto exported = exportLibrary(dir / "never.foslib", broken, AppInfo{});
    ASSERT_FALSE(exported.ok());
    EXPECT_EQ(exported.error().code, ErrorCode::InvalidArgument);
    EXPECT_THAT(exported.error().message, HasSubstr("/materials/2/id: duplicate id \"mat.pb18\""));
    EXPECT_FALSE(std::filesystem::exists(dir / "never.foslib"));
}

TEST(Library, ImportRejectsOtherKindsNewerVersionsAndInvalidEntries) {
    TempDir dir;
    Document project;
    project.format = "furnitureos-project";
    ASSERT_TRUE(saveDocument(dir / "p.fosp", project).ok());
    EXPECT_EQ(importLibrary(dir / "p.fosp").error().code, ErrorCode::Unsupported);

    Document future;
    future.format = std::string(kLibraryFormatKind);
    future.formatVersion = 2;
    future.payload = libraryToJson(fixtureLibrary());
    ASSERT_TRUE(saveDocument(dir / "future.foslib", future).ok());
    auto newer = importLibrary(dir / "future.foslib");
    ASSERT_FALSE(newer.ok());
    EXPECT_EQ(newer.error().code, ErrorCode::UnsupportedVersion);

    Document invalid;
    invalid.format = std::string(kLibraryFormatKind);
    invalid.payload = libraryToJson(fixtureLibrary());
    invalid.payload["materials"][1]["thickness"] = -3;
    ASSERT_TRUE(saveDocument(dir / "bad.foslib", invalid).ok());
    EXPECT_THAT(importLibrary(dir / "bad.foslib").error().message,
                HasSubstr("/payload/materials/1/thickness: expected a positive number, got -3"));

    EXPECT_EQ(importLibrary(dir / "missing.foslib").error().code, ErrorCode::IoError);
}

TEST(Library, UnknownPayloadAndEntryKeysSurviveImportExport) {
    TempDir dir;
    Document doc;
    doc.format = std::string(kLibraryFormatKind);
    doc.payload = libraryToJson(fixtureLibrary());
    doc.payload["collections"] = Json::parse(R"([{"name": "Oak decors", "members": ["mat.pb18"]}])");
    doc.payload["materials"][0]["density"] = 650;
    ASSERT_TRUE(saveDocument(dir / "a.foslib", doc).ok());

    auto imported = importLibrary(dir / "a.foslib");
    ASSERT_TRUE(imported.ok()) << imported.error().message;
    EXPECT_EQ(imported.value().extra["collections"][0]["name"], "Oak decors");
    ASSERT_TRUE(exportLibrary(dir / "b.foslib", imported.value(), AppInfo{}).ok());
    const Json again = Json::parse(formats::test::readText(dir / "b.foslib"))["payload"];
    EXPECT_EQ(nlohmann::json(again), nlohmann::json(doc.payload));
}

TEST(Library, ChecksumWarningIsForwarded) {
    TempDir dir;
    ASSERT_TRUE(exportLibrary(dir / "a.foslib", fixtureLibrary(), AppInfo{}).ok());
    std::string text = formats::test::readText(dir / "a.foslib");
    const auto pos = text.find("HDF 3 mm");
    ASSERT_NE(pos, std::string::npos);
    text.replace(pos, 3, "MDF");
    formats::test::writeText(dir / "a.foslib", text);

    EXPECT_EQ(importLibrary(dir / "a.foslib").error().code, ErrorCode::CorruptData);
    LoadOptions lenient;
    lenient.allowChecksumMismatch = true;
    core::Diagnostics warnings;
    auto imported = importLibrary(dir / "a.foslib", lenient, &warnings);
    ASSERT_TRUE(imported.ok()) << imported.error().message;
    EXPECT_EQ(imported.value().materials[1].name, "MDF 3 mm");
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_EQ(warnings[0].code, "formats.container.checksum_mismatch");
}

// ---------------------------------------------------------------- merge

namespace {
/// Target: A(16), B(18).  Incoming: B'(19, same id as B), C(25).
struct MergeCase {
    Library target, incoming;
    MergeCase() {
        target.materials = {material("A", "Board A", 16), material("B", "Board B", 18)};
        incoming.materials = {material("B", "Board B new", 19), material("C", "Board C", 25)};
        domain::EdgeBand band;
        band.id = domain::EdgeBandId("B"); // same text as a material id: categories are independent
        band.name = "Band";
        incoming.edgeBands = {band};
    }
};
} // namespace

TEST(LibraryMerge, SkipKeepsExistingEntries) {
    MergeCase c;
    auto report = mergeLibrary(c.target, c.incoming, ConflictPolicy::Skip);
    ASSERT_TRUE(report.ok()) << report.error().message;
    ASSERT_EQ(c.target.materials.size(), 3u);
    EXPECT_EQ(c.target.materials[1].name, "Board B"); // untouched
    EXPECT_EQ(c.target.materials[1].thickness, 18);
    EXPECT_EQ(c.target.materials[2].id.str(), "C");
    ASSERT_EQ(c.target.edgeBands.size(), 1u);

    ASSERT_EQ(report.value().entries.size(), 3u);
    const MergeEntry& b = report.value().entries[0];
    EXPECT_EQ(b.category, LibraryCategory::Material);
    EXPECT_EQ(b.action, MergeAction::Skipped);
    EXPECT_EQ(b.incomingId, "B");
    EXPECT_EQ(b.resultId, "B");
    EXPECT_EQ(b.name, "Board B new");
    EXPECT_EQ(report.value().entries[1].action, MergeAction::Added);
    EXPECT_EQ(report.value().entries[2].category, LibraryCategory::EdgeBand);
    EXPECT_EQ(report.value().entries[2].action, MergeAction::Added); // no conflict across categories
    EXPECT_EQ(report.value().count(MergeAction::Added), 2u);
    EXPECT_EQ(report.value().count(MergeAction::Skipped), 1u);
    EXPECT_EQ(report.value().count(MergeAction::Replaced), 0u);
}

TEST(LibraryMerge, ReplaceOverwritesInPlace) {
    MergeCase c;
    auto report = mergeLibrary(c.target, c.incoming, ConflictPolicy::Replace);
    ASSERT_TRUE(report.ok());
    ASSERT_EQ(c.target.materials.size(), 3u);
    EXPECT_EQ(c.target.materials[0].id.str(), "A");
    EXPECT_EQ(c.target.materials[1].id.str(), "B"); // same position
    EXPECT_EQ(c.target.materials[1].name, "Board B new");
    EXPECT_EQ(c.target.materials[1].thickness, 19);
    EXPECT_EQ(c.target.materials[2].id.str(), "C");
    EXPECT_EQ(report.value().entries[0].action, MergeAction::Replaced);
    EXPECT_EQ(report.value().count(MergeAction::Replaced), 1u);
    EXPECT_EQ(report.value().count(MergeAction::Added), 2u);
}

TEST(LibraryMerge, RenameAddsTheIncomingEntryUnderANewId) {
    MergeCase c;
    // The generator first proposes ids that are unusable: empty, an existing target id,
    // an id of another incoming entry; only "B-2" is free.
    auto report = mergeLibrary(c.target, c.incoming, ConflictPolicy::RenameWithNewId, sequence({"", "A", "C", "B-2"}));
    ASSERT_TRUE(report.ok()) << report.error().message;
    ASSERT_EQ(c.target.materials.size(), 4u);
    EXPECT_EQ(c.target.materials[1].name, "Board B"); // existing one kept
    EXPECT_EQ(c.target.materials[2].id.str(), "B-2");
    EXPECT_EQ(c.target.materials[2].name, "Board B new");
    EXPECT_EQ(c.target.materials[3].id.str(), "C");
    const MergeEntry& renamed = report.value().entries[0];
    EXPECT_EQ(renamed.action, MergeAction::Renamed);
    EXPECT_EQ(renamed.incomingId, "B");
    EXPECT_EQ(renamed.resultId, "B-2");
    EXPECT_EQ(report.value().count(MergeAction::Renamed), 1u);
}

TEST(LibraryMerge, DefaultGeneratorProducesFreshUniqueIds) {
    MergeCase c;
    auto report = mergeLibrary(c.target, c.incoming, ConflictPolicy::RenameWithNewId);
    ASSERT_TRUE(report.ok());
    const std::string fresh = report.value().entries[0].resultId;
    EXPECT_EQ(fresh.size(), 36u); // UUID text
    EXPECT_EQ(c.target.materials[2].id.str(), fresh);
    EXPECT_TRUE(libraryFromJson(libraryToJson(c.target)).ok()); // still free of duplicates
}

TEST(LibraryMerge, ErrorsLeaveTheTargetUnchanged) {
    MergeCase c;
    const Json before = libraryToJson(c.target);

    auto exhausted = mergeLibrary(c.target, c.incoming, ConflictPolicy::RenameWithNewId, [] { return std::string("A"); });
    ASSERT_FALSE(exhausted.ok());
    EXPECT_THAT(exhausted.error().message, HasSubstr("Cannot rename the imported material \"B\""));
    EXPECT_EQ(libraryToJson(c.target), before);

    Library duplicated = c.incoming;
    duplicated.materials.push_back(material("C", "Board C again", 30));
    auto dup = mergeLibrary(c.target, duplicated, ConflictPolicy::Replace);
    ASSERT_FALSE(dup.ok());
    EXPECT_EQ(dup.error().code, ErrorCode::InvalidArgument);
    EXPECT_THAT(dup.error().message, HasSubstr("the imported library contains the material id \"C\" more than once"));
    EXPECT_EQ(libraryToJson(c.target), before);
}

TEST(LibraryMerge, MergingIntoAnEmptyLibraryAddsEverything) {
    Library target;
    auto report = mergeLibrary(target, fixtureLibrary(), ConflictPolicy::Skip);
    ASSERT_TRUE(report.ok());
    EXPECT_EQ(report.value().entries.size(), 5u); // 2 materials + 2 edge bands + 1 hardware
    EXPECT_EQ(report.value().count(MergeAction::Added), 5u);
    EXPECT_EQ(report.value().entries[4].category, LibraryCategory::Hardware);
    EXPECT_EQ(std::string(toString(report.value().entries[4].category)), "hardware definition");
    EXPECT_EQ(std::string(toString(MergeAction::Renamed)), "renamed");
    EXPECT_EQ(target.hardware.size(), 1u);
}
