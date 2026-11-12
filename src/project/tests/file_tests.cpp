// SPDX-License-Identifier: AGPL-3.0-only
#include "test_support.hpp"

#include "fos/furniture/edit.hpp"
#include "fos/project/codec.hpp"

namespace fos::project::test {
namespace {

const char* kSavedAt = "2026-11-12T12:00:00Z";

/// A project that uses every part of the schema.
Project richProject() {
    Project p = newProject(meta());
    p.metadata.description = "Ünïcode description";
    Item cab = baseCabinetItem();
    furniture::CabinetDefinition& c = *cab.cabinet();
    c.placement = geometry::Transform::rotationZ(90.0) * geometry::Transform::translation({120.5, 0, 0});
    const furniture::Libraries& l = p.libraries;
    EXPECT_TRUE(furniture::setParameter(c, l, "width", 800.25).ok());
    EXPECT_TRUE(furniture::setParameterExpression(c, l, "shelf_depth", "interior_depth - 37").ok()); // pins
    EXPECT_TRUE(furniture::addParameter(c, l, parametric::ParameterDef::constant("gap_extra", 1.5, parametric::ParameterKind::Length)).ok());
    EXPECT_TRUE(furniture::splitSection(c, l, "", furniture::SplitDirection::Vertical,
                                        {furniture::SectionSize::weight(1), furniture::SectionSize::fixed(300.1)}).ok());
    EXPECT_TRUE(furniture::setShelfCount(c, l, "2", 3).ok());
    furniture::PartOverride o;
    o.material = domain::MaterialId(defaults::kMdf18);
    o.edges[domain::EdgeSide::L2] = std::nullopt;
    o.edges[domain::EdgeSide::L1] = domain::EdgeBandId(defaults::kAbs04);
    EXPECT_TRUE(furniture::setPartOverride(c, l, "side.left", o).ok());
    p.items.push_back(cab);

    PanelItem panel;
    panel.id = domain::AssemblyId("worktop");
    panel.placement = geometry::Transform::translation({0, -20, 820});
    panel.panel = furniture::makePanel(domain::PartId("worktop/panel"), "Worktop", domain::MaterialId(defaults::kMdf18));
    panel.panel.role = domain::PartRole::Worktop;
    panel.panel.edges[0] = domain::EdgeBandId(defaults::kAbs2);
    EXPECT_TRUE(panel.panel.parameters.defineConstant("overhang", 20, parametric::ParameterKind::Length).ok());
    EXPECT_TRUE(panel.panel.parameters.setExpression("length", "800.25 + 2 * overhang").ok());
    p.items.push_back(Item{panel, formats::Json::object()});

    drawings::DrawingEdits e;
    drawings::ElementEdit ee;
    ee.offset = -12.5;
    ee.textShift = {1.25, -2};
    ee.hidden = false;
    ee.textOverride = "800 (approx.)";
    ee.measuredAtEdit = 800.25;
    e.elements["cab1/view.front/dim.overall.width"] = ee;
    e.texts.push_back({"t1", "cab1/view.front", {10, 20}, "Note", 3.5, 15});
    e.leaders.push_back({"l1", "cab1/view.front", {5, 6}, {10, 10}, "Leader", 2.5});
    drawings::UserDimension d;
    d.id = "d1";
    d.viewId = "cab1/view.front";
    d.orientation = drawings::DimensionOrientation::Vertical;
    d.p1 = {0, 0};
    d.p2 = {0, 100};
    d.anchor1 = drawings::ModelAnchor{"cab1/bottom", 3, {}};
    d.anchor2 = drawings::ModelAnchor{"cab1/shelf.1.1", std::nullopt, {1, 2, 3}};
    d.textOverride = "x";
    d.measuredAtEdit = 101.5;
    e.dimensions.push_back(d);
    e.detailMarkers.push_back({"m1", "cab1/view.front", {50, 50}, 40, "A"});
    p.drawingEdits[assemblyDrawingId("cab1")] = e;

    CuttingSettings cs;
    cs.kerf = 3.2;
    cs.trim = {5, 6, 7, 8};
    cs.maxStages = 3;
    cs.firstCut = cutting::FirstCut::Lengthwise;
    cs.objective = cutting::Objective::PreferRemnants;
    cs.remnants.push_back({"r1", "Offcut", 1200, 600, 2});
    p.cutting.materials[defaults::kParticleboard18] = cs;
    p.settings.backupCount = 2;
    p.settings.edgeBanding.overhangPerEnd = 30;
    p.custom = formats::Json::parse(R"({"crm": {"order": 4711, "tags": ["a", "b"]}, "note": null})");
    return p;
}

/// Rewrites the payload of a file text and re-seals the checksum.
std::string rewrite(const std::string& text, const std::function<void(formats::Json&)>& edit) {
    auto loaded = formats::parseDocument(text, projectFormatSpec());
    EXPECT_TRUE(loaded.ok());
    formats::Document d = loaded.value().document;
    edit(d.payload);
    auto out = formats::serializeDocument(d);
    EXPECT_TRUE(out.ok());
    return out.value();
}

std::string serialized(const Project& p) {
    auto s = serializeProject(p, kSavedAt);
    EXPECT_TRUE(s.ok()) << (s.ok() ? "" : s.error().message);
    return s.ok() ? s.value() : std::string();
}

} // namespace

TEST(File, EnvelopeShape) {
    const auto text = serialized(newProject(meta()));
    const auto j = formats::Json::parse(text);
    EXPECT_EQ(j["format"], "furnitureos-project");
    EXPECT_EQ(j["formatVersion"], 1);
    EXPECT_EQ(j["savedAt"], kSavedAt);
    EXPECT_EQ(j["payload"]["project"]["units"], "mm");
    EXPECT_EQ(j["checksum"]["algorithm"], "sha256");
}

TEST(File, SaveLoadSaveIsByteIdenticalAndLossless) {
    TempDir dir;
    const auto path = dir.path / "kitchen.fosp";
    const Project p = richProject();
    const Resolved before = resolved(p);
    ASSERT_TRUE(saveProject(path, p, fixedClock()).ok());
    const std::string first = formats::readFile(path).value();

    auto loaded = loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    EXPECT_EQ(loaded.value().fileVersion, 1);
    EXPECT_TRUE(loaded.value().migrations.empty());
    EXPECT_EQ(modelText(loaded.value().resolved.model), modelText(before.model));
    EXPECT_EQ(projectToJson(loaded.value().project), projectToJson(p));
    EXPECT_EQ(loaded.value().project.custom, p.custom);

    ASSERT_TRUE(saveProject(path, loaded.value().project, fixedClock()).ok());
    EXPECT_EQ(formats::readFile(path).value(), first);
    EXPECT_TRUE(std::filesystem::exists(formats::backupPath(path, 1))); // previous version kept
    EXPECT_EQ(formats::readFile(formats::backupPath(path, 1)).value(), first);
}

TEST(File, UnknownKeysArePreserved) {
    const std::string base = serialized(richProject());
    const std::string text = rewrite(base, [](formats::Json& payload) {
        payload["pluginData"] = {{"x", 1}};
        payload["project"]["futureField"] = "keep me";
        payload["settings"]["theme"] = {{"dark", true}};
        payload["items"][0]["rating"] = 5;
        payload["libraries"]["materials"][0]["density"] = 650; // formats: kept in Material::custom
    });
    auto loaded = parseProject(text);
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    const Project& p = loaded.value().project;
    EXPECT_EQ(p.extra["pluginData"]["x"], 1);
    EXPECT_EQ(p.metadata.extra["futureField"], "keep me");
    EXPECT_EQ(p.items[0].extra["rating"], 5);
    auto again = serializeProject(p, kSavedAt);
    ASSERT_TRUE(again.ok());
    EXPECT_EQ(again.value(), text); // byte-identical, unknown keys included
}

TEST(File, UnknownKeyInClosedObjectIsRejectedWithPath) {
    const std::string text = rewrite(serialized(richProject()), [](formats::Json& payload) {
        payload["items"][0]["interior"]["children"][1]["colour"] = "red";
    });
    auto loaded = parseProject(text);
    ASSERT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.error().code, core::ErrorCode::ParseError);
    EXPECT_EQ(loaded.error().context, "/payload/items/0/interior/children/1/colour");
    EXPECT_NE(loaded.error().message.find("unknown key \"colour\""), std::string::npos);
}

TEST(File, StrictValidationMessages) {
    struct Case {
        std::function<void(formats::Json&)> edit;
        std::string path;
    };
    const Case cases[] = {
        {[](formats::Json& p) { p["items"][0]["interior"]["children"][1]["shelfCount"] = -1; },
         "/payload/items/0/interior/children/1/shelfCount"},
        {[](formats::Json& p) { p["items"][0]["construction"]["back"] = "overlay"; }, "/payload/items/0/construction/back"},
        {[](formats::Json& p) { p["project"]["units"] = "inch"; }, "/payload/project/units"},
        {[](formats::Json& p) { p["items"][1]["type"] = "sofa"; }, "/payload/items/1/type"},
        {[](formats::Json& p) { p["cutting"]["materials"]["mat.generic.pb18"]["kerf"] = -1; },
         "/payload/cutting/materials/mat.generic.pb18/kerf"},
        {[](formats::Json& p) { p["items"][1]["id"] = "cab1"; }, "/payload/items/1/id"},
        {[](formats::Json& p) { auto& d = p["items"][0]["parameters"][0]; d.erase("value"); d.erase("expression"); }, "/payload/items/0/parameters/0"},
        {[](formats::Json& p) { p["custom"] = 3; }, "/payload/custom"},
    };
    const std::string base = serialized(richProject());
    for (const auto& c : cases) {
        auto loaded = parseProject(rewrite(base, c.edit));
        ASSERT_FALSE(loaded.ok()) << c.path;
        EXPECT_EQ(loaded.error().context, c.path) << loaded.error().message;
        EXPECT_EQ(loaded.error().message.rfind(c.path + ": ", 0), 0u) << loaded.error().message;
    }
}

TEST(File, BrokenParameterGraphIsReported) {
    auto loaded = parseProject(rewrite(serialized(richProject()), [](formats::Json& p) {
        for (auto& def : p["items"][0]["parameters"])
            if (def["name"] == "inner_width") def["expression"] = "width - 2 * nonexistent";
    }));
    ASSERT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.error().context, "/payload/items/0/parameters");
    EXPECT_NE(loaded.error().message.find("nonexistent"), std::string::npos);
}

TEST(File, NewerFormatVersionIsRefused) {
    formats::Document d = toDocument(newProject(meta()), kSavedAt);
    d.formatVersion = 2;
    const std::string text = formats::serializeDocument(d).value();
    auto loaded = parseProject(text);
    ASSERT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.error().code, core::ErrorCode::UnsupportedVersion);
    EXPECT_NE(loaded.error().message.find("2"), std::string::npos);
}

TEST(File, OtherKindIsRefused) {
    formats::Document d = toDocument(newProject(meta()), kSavedAt);
    d.format = "furnitureos-library";
    auto loaded = parseProject(formats::serializeDocument(d).value());
    ASSERT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.error().code, core::ErrorCode::Unsupported);
}

TEST(File, TruncatedFilesFailCleanly) {
    const std::string text = serialized(richProject());
    for (std::size_t cut : {std::size_t{0}, std::size_t{1}, std::size_t{17}, text.size() / 3, text.size() / 2,
                            text.size() - 200, text.size() - 3, text.size() - 2}) {
        auto loaded = parseProject(std::string_view(text).substr(0, cut));
        ASSERT_FALSE(loaded.ok()) << "cut at " << cut;
        EXPECT_FALSE(loaded.error().message.empty());
    }
    // Every 97th offset as well: never a crash, never a success.
    for (std::size_t cut = 0; cut + 2 < text.size(); cut += 97) EXPECT_FALSE(parseProject(text.substr(0, cut)).ok());
}

TEST(File, ChecksumMismatch) {
    std::string text = serialized(newProject(meta()));
    const auto at = text.find("\"Kitchen\"");
    ASSERT_NE(at, std::string::npos);
    text.replace(at, 9, "\"Kitchem\"");
    auto loaded = parseProject(text);
    ASSERT_FALSE(loaded.ok());
    EXPECT_EQ(loaded.error().code, core::ErrorCode::CorruptData);
    formats::LoadOptions recover;
    recover.allowChecksumMismatch = true;
    auto opened = parseProject(text, recover);
    ASSERT_TRUE(opened.ok());
    EXPECT_EQ(opened.value().project.metadata.name, "Kitchem");
    ASSERT_FALSE(opened.value().diagnostics.empty());
    EXPECT_EQ(opened.value().diagnostics[0].code, "formats.container.checksum_mismatch");
}

TEST(File, ItemThatCannotBeGeneratedStillLoads) {
    Project p = newProject(meta());
    Item bad = baseCabinetItem();
    bad.cabinet()->materials.carcass = domain::MaterialId("mat.deleted");
    p.items.push_back(bad);
    auto loaded = parseProject(serialized(p));
    ASSERT_TRUE(loaded.ok());
    EXPECT_FALSE(loaded.value().resolved.items[0].generated);
    EXPECT_TRUE(core::hasErrors(loaded.value().diagnostics));
}

TEST(File, UnsavableProjectIsNotWritten) {
    TempDir dir;
    const auto path = dir.path / "x.fosp";
    Project p = newProject(meta());
    Item cab = baseCabinetItem();
    cab.cabinet()->placement.xAxis = {2, 0, 0}; // scaled: not rigid
    p.items.push_back(cab);
    auto r = saveProject(path, p);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("would not load back"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(File, AutosaveAndRecovery) {
    TempDir dir;
    const auto path = dir.path / "k.fosp";
    Project p = newProject(meta());
    ASSERT_TRUE(saveProject(path, p).ok());
    p.items.push_back(baseCabinetItem());
    ASSERT_TRUE(writeProjectAutosave(path, p).ok());
    EXPECT_TRUE(std::filesystem::exists(formats::autosavePath(path)));
    EXPECT_TRUE(loadProject(path).value().project.items.empty()); // file untouched
    auto rec = recoverProject(path);
    ASSERT_TRUE(rec.ok()) << rec.error().message;
    EXPECT_EQ(rec.value().project.items.size(), 1u);
}

TEST(File, MigrationRegistryIsWired) {
    const formats::FormatSpec spec = projectFormatSpec();
    EXPECT_EQ(spec.kind, kProjectFormatKind);
    EXPECT_EQ(spec.currentVersion, kProjectFormatVersion);
    EXPECT_FALSE(spec.migrations.has(1)); // version 1 is the first version
    auto missing = loadProject("/nonexistent/dir/file.fosp");
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().code, core::ErrorCode::IoError);
}

} // namespace fos::project::test
