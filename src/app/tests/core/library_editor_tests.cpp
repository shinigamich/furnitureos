// SPDX-License-Identifier: AGPL-3.0-only
// Library editor view-model: rows with usage, validation, one undo step per Apply, refused
// removal of entries in use, hardware edits that keep the drilling geometry, .foslib round trip.
#include "fos/app/command_text.hpp"
#include "fos/app/library_editor.hpp"
#include "test_support.hpp"

namespace fos::app::test {
namespace {

const domain::Material* material(const Session& s, const std::string& id) {
    return s.project().libraries.findMaterial(domain::MaterialId(id));
}

TEST(LibraryEditor, RowsShowUsageAndRemovalInUseIsRefused) {
    Session s;
    ASSERT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet).ok());
    const std::string item = s.project().items.at(0).id();
    const auto rows = libraryRows(s.project(), LibraryCategory::Material);
    ASSERT_EQ(rows.size(), s.project().libraries.materials.size());
    const auto pb = std::find_if(rows.begin(), rows.end(), [](const LibraryRow& r) { return r.id == project::defaults::kParticleboard18; });
    ASSERT_NE(pb, rows.end());
    EXPECT_EQ(pb->usedBy, std::vector<std::string>{item});
    EXPECT_TRUE(LibraryText::rowCaption(*pb).contains(QStringLiteral("used by")));

    const std::size_t before = s.project().libraries.materials.size();
    const auto r = removeLibraryEntry(s, LibraryCategory::Material, project::defaults::kParticleboard18);
    ASSERT_EQ(r.size(), 1);
    EXPECT_TRUE(r[0].contains(QStringLiteral("cannot be removed"))) << r[0].toStdString();
    EXPECT_TRUE(r[0].contains(QString::fromStdString(item))) << r[0].toStdString();
    EXPECT_EQ(s.project().libraries.materials.size(), before);
}

TEST(LibraryEditor, MaterialValidationApplyAndUndo) {
    Session s;
    s.newProject();
    const std::size_t before = s.project().libraries.materials.size();
    domain::Material m = newMaterial(s.project());
    EXPECT_EQ(m.id.str(), "material1");
    EXPECT_EQ(m.name, "New material");

    domain::Material bad = m;
    bad.name = "  ";
    bad.thickness = 0;
    bad.costPerSquareMeter = -2;
    const auto problems = applyMaterial(s, bad, true);
    ASSERT_EQ(problems.size(), 3) << problems.join(QLatin1Char('\n')).toStdString(); // name, thickness, cost per m²
    EXPECT_TRUE(problems[1].contains(QStringLiteral("Thickness")));
    EXPECT_EQ(s.project().libraries.materials.size(), before);

    domain::Material taken = m;
    taken.id = domain::MaterialId(project::defaults::kMdf18);
    ASSERT_EQ(applyMaterial(s, taken, true).size(), 1); // id already used
    taken.id = domain::MaterialId(" x");
    ASSERT_EQ(applyMaterial(s, taken, true).size(), 1); // leading space

    m.thickness = 19;
    m.costPerSquareMeter = 12.5;
    ASSERT_TRUE(applyMaterial(s, m, true).isEmpty());
    ASSERT_EQ(s.project().libraries.materials.size(), before + 1);
    EXPECT_EQ(material(s, "material1")->costPerSquareMeter, 12.5);
    EXPECT_EQ(commandText(project::commands::upsertMaterial(m).label, s.project()),
              QStringLiteral("Edit material New material"));

    // Editing the existing entry (id not re-checked) is a second step; each undo reverts one.
    m.available = false;
    ASSERT_TRUE(applyMaterial(s, m, false).isEmpty());
    EXPECT_FALSE(material(s, "material1")->available);
    ASSERT_TRUE(s.undo().ok());
    EXPECT_TRUE(material(s, "material1")->available);
    ASSERT_TRUE(s.undo().ok());
    EXPECT_EQ(material(s, "material1"), nullptr);

    // Unused entries can be removed (one step).
    ASSERT_TRUE(applyMaterial(s, m, true).isEmpty());
    ASSERT_TRUE(removeLibraryEntry(s, LibraryCategory::Material, "material1").isEmpty());
    EXPECT_EQ(material(s, "material1"), nullptr);
}

TEST(LibraryEditor, EdgeBandAndDuplicates) {
    Session s;
    s.newProject();
    domain::EdgeBand e = newEdgeBand(s.project());
    EXPECT_EQ(e.id.str(), "edge1");
    e.thickness = 2;
    e.width = 1.5; // narrower than thick
    const auto p = validateEdgeBand(s.project(), e, true);
    ASSERT_EQ(p.size(), 1);
    EXPECT_TRUE(p[0].contains(QStringLiteral("1.5"))) << p[0].toStdString();
    e.width = 23;
    e.costPerMeter = 0.8;
    ASSERT_TRUE(applyEdgeBand(s, e, true).isEmpty());

    const domain::EdgeBand d1 = duplicateEdgeBand(s.project(), e);
    EXPECT_EQ(d1.id.str(), "edge1-copy");
    EXPECT_EQ(d1.name, "New edge band (copy)");
    EXPECT_EQ(d1.costPerMeter, 0.8);
    ASSERT_TRUE(applyEdgeBand(s, d1, true).isEmpty());
    EXPECT_EQ(duplicateEdgeBand(s.project(), e).id.str(), "edge1-copy2");
}

TEST(LibraryEditor, HardwareEditKeepsKindParamsAndVerification) {
    Session s;
    s.newProject();
    ASSERT_FALSE(s.project().libraries.hardware.empty());
    const domain::HardwareDefinition original = s.project().libraries.hardware.front();
    HardwareInput in = hardwareInput(original);
    in.name = "Hinge X";
    in.code = "ART-1";
    in.unitCost = 3.25;
    const domain::HardwareDefinition edited = applyHardwareInput(original, in);
    EXPECT_EQ(edited.kind, original.kind);
    EXPECT_EQ(edited.params, original.params);
    EXPECT_EQ(edited.dimensionsVerified, original.dimensionsVerified);
    ASSERT_TRUE(applyHardware(s, edited, false).isEmpty());
    const auto* stored = s.project().libraries.findHardware(original.id);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->article, "ART-1");
    EXPECT_EQ(stored->unitCost, 3.25);
    EXPECT_EQ(stored->params, original.params);

    in.unitCost = -1;
    EXPECT_EQ(applyHardware(s, applyHardwareInput(original, in), false).size(), 1);
    const domain::HardwareDefinition copy = duplicateHardware(s.project(), *stored);
    EXPECT_EQ(copy.id.str(), original.id.str() + "-copy");
    EXPECT_EQ(copy.params, original.params);
}

TEST(LibraryEditor, ColorText) {
    EXPECT_EQ(colorText({255, 16, 0}), "#ff1000");
    const auto c = parseColorText("A0b1C2");
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(*c, (domain::Color{0xA0, 0xB1, 0xC2}));
    EXPECT_FALSE(parseColorText("#12345").has_value());
    EXPECT_FALSE(parseColorText("#12345g").has_value());
}

TEST(LibraryEditor, ExportImportIsOneUndoStep) {
    TempDir dir;
    Session s;
    s.newProject();
    const auto path = dir.path / "lib.foslib";
    ASSERT_TRUE(exportProjectLibrary(s.project(), path).ok());
    auto loaded = formats::importLibrary(path);
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    formats::Library incoming = loaded.value();
    const std::size_t materials = s.project().libraries.materials.size();
    ASSERT_EQ(incoming.materials.size(), materials);

    // Every incoming id exists: Rename adds each under "<id>-imported".
    incoming.materials.front().costPerSheet = 99;
    auto preview = previewLibraryImport(s.project(), incoming, formats::ConflictPolicy::RenameWithNewId);
    ASSERT_TRUE(preview.ok()) << preview.error().message;
    EXPECT_EQ(preview.value().count(formats::MergeAction::Renamed), preview.value().entries.size());
    EXPECT_EQ(preview.value().entries.front().resultId, incoming.materials.front().id.str() + "-imported");

    // Replace overwrites in place in one step; undo restores the old price.
    const std::string first = incoming.materials.front().id.str();
    const double oldCost = material(s, first)->costPerSheet;
    const auto cmd = importLibraryCommand(incoming, formats::ConflictPolicy::Replace, "lib.foslib");
    EXPECT_EQ(commandText(cmd.label, s.project()), QStringLiteral("Import library lib.foslib"));
    ASSERT_TRUE(s.execute(cmd).ok());
    EXPECT_EQ(material(s, first)->costPerSheet, 99);
    EXPECT_EQ(s.project().libraries.materials.size(), materials);
    ASSERT_TRUE(s.undo().ok());
    EXPECT_EQ(material(s, first)->costPerSheet, oldCost);

    ASSERT_TRUE(s.execute(importLibraryCommand(incoming, formats::ConflictPolicy::RenameWithNewId, "lib")).ok());
    EXPECT_EQ(s.project().libraries.materials.size(), 2 * materials);
    ASSERT_NE(material(s, first + "-imported"), nullptr);
    EXPECT_EQ(material(s, first + "-imported")->costPerSheet, 99);
    ASSERT_TRUE(s.undo().ok());
    EXPECT_EQ(s.project().libraries.materials.size(), materials);
}

} // namespace
} // namespace fos::app::test
