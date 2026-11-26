// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/project/codec.hpp"
#include "fos/project/components.hpp"
#include "test_support.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>
#include <tuple>

using namespace fos;
using namespace fos::project;
using formats::ConflictPolicy;
using formats::MergeAction;
using ::testing::HasSubstr;

namespace {

Project sourceProject() {
    Project p = newProject(test::meta());
    p.items.push_back(test::baseCabinetItem("cab1"));
    return p;
}

Component baseComponent() {
    formats::ComponentMeta meta;
    meta.name = "Base 600";
    meta.category = "Base cabinets";
    meta.tags = {"kitchen", "base"};
    auto c = makeComponent(sourceProject(), "cab1", meta);
    EXPECT_TRUE(c.ok()) << (c.ok() ? "" : c.error().message);
    return c.value();
}

/// (part id without the "<item>/" prefix, length, width, thickness), sorted.
using PartSize = std::tuple<std::string, double, double, double>;
std::vector<PartSize> partSizes(const Resolved& r, const std::string& itemId) {
    std::vector<PartSize> out;
    for (const auto& a : r.model.assemblies)
        if (a.id.str() == itemId)
            for (const auto& part : a.parts) {
                std::string id = part.id.str();
                if (id.rfind(itemId + "/", 0) == 0) id = id.substr(itemId.size() + 1);
                out.emplace_back(id, part.length, part.width, part.thickness);
            }
    std::sort(out.begin(), out.end());
    return out;
}

double sideThickness(const Project& p, const std::string& itemId) {
    for (const auto& [id, l, w, t] : partSizes(test::resolved(p), itemId))
        if (id.find("side") != std::string::npos) return t;
    return -1;
}

TEST(Components, MakeComponentEmbedsExactlyTheReferencedEntries) {
    const Component c = baseComponent();
    EXPECT_EQ(c.meta.id, "comp.base-600"); // slug of "Base 600"
    // Template materials (project.hpp): carcass pb18, back hdf3, front = carcass; edges abs2 / abs04.
    std::vector<std::string> mats;
    for (const auto& m : c.library.materials) mats.push_back(m.id.str());
    EXPECT_EQ(mats, (std::vector<std::string>{defaults::kHdf3, defaults::kParticleboard18}));
    EXPECT_EQ(libraryReferences(c.item).hardware.size(), c.library.hardware.size());
    // Preview = bounding box of the template: 600 wide, 820 high, 560 deep (test_support.hpp).
    EXPECT_NEAR(c.meta.previewSize.width, 600, 1e-9);
    EXPECT_NEAR(c.meta.previewSize.height, 820, 1e-9);
    EXPECT_GE(c.meta.previewSize.depth, 560 - 1e-9);
    EXPECT_FALSE(makeComponent(sourceProject(), "nope", {}).ok());
}

TEST(Components, SavedBaseCabinetInsertsIntoNewProjectWithIdenticalPartSizes) {
    test::TempDir dir;
    const auto file = dir.path / "base.foscomp";
    ASSERT_TRUE(saveComponents(file, {baseComponent()}).ok());
    auto loaded = loadComponents(file);
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    const Component* c = findComponent(loaded.value(), "Base 600");
    ASSERT_NE(c, nullptr);

    Metadata other = test::meta();
    other.id = "6a1c2d3e-0000-4000-8000-000000000001";
    Project target = newProject(other);
    target.libraries.materials.clear(); // the component must bring its own materials
    target.libraries.edgeBands.clear();
    target.libraries.hardware.clear();
    Document doc(target);
    const auto at = geometry::Transform::translation({1000, 0, 0});
    ASSERT_TRUE(doc.execute(insertComponent(*c, "k1", at, ConflictPolicy::Skip)).ok());
    const Project& p = doc.project();
    ASSERT_EQ(p.items.size(), 1u);
    EXPECT_EQ(p.items[0].name(), "Base 600");
    EXPECT_EQ(p.items[0].placement().applyPoint({0, 0, 0}).x, 1000);
    EXPECT_EQ(p.libraries.materials.size(), c->library.materials.size());

    const auto original = partSizes(test::resolved(sourceProject()), "cab1");
    const auto inserted = partSizes(test::resolved(p), "k1");
    ASSERT_FALSE(original.empty());
    EXPECT_EQ(inserted, original);

    ASSERT_TRUE(doc.undo().ok());
    EXPECT_TRUE(doc.project().items.empty());
    EXPECT_TRUE(doc.project().libraries.materials.empty());
    // Id in use -> refused.
    ASSERT_TRUE(doc.redo().ok());
    EXPECT_FALSE(doc.execute(insertComponent(*c, "k1", at, ConflictPolicy::Skip)).ok());
}

/// Target whose pb18 has the same id but 16 mm thickness (component: 18 mm).
Project conflictingTarget() {
    Project p = newProject(test::meta());
    for (auto& m : p.libraries.materials)
        if (m.id.str() == defaults::kParticleboard18) {
            m.thickness = 16;
            m.name = "Local board";
        }
    return p;
}

const domain::Material* material(const Project& p, const std::string& id) {
    for (const auto& m : p.libraries.materials)
        if (m.id.str() == id) return &m;
    return nullptr;
}

TEST(Components, EmbeddedMaterialsMergeWithEachConflictPolicy) {
    const Component c = baseComponent();
    const std::size_t before = conflictingTarget().libraries.materials.size();
    {   // Skip: existing 16 mm board kept and used.
        Project p = conflictingTarget();
        ASSERT_TRUE(insertComponent(c, "k1", {}, ConflictPolicy::Skip).apply(p).ok());
        EXPECT_EQ(material(p, defaults::kParticleboard18)->thickness, 16);
        EXPECT_EQ(p.libraries.materials.size(), before);
        EXPECT_EQ(sideThickness(p, "k1"), 16);
    }
    {   // Replace: the entry is overwritten in place by the 18 mm board.
        Project p = conflictingTarget();
        auto r = insertComponent(c, "k1", {}, ConflictPolicy::Replace).apply(p);
        ASSERT_TRUE(r.ok());
        EXPECT_TRUE(r.value().libraries);
        EXPECT_EQ(material(p, defaults::kParticleboard18)->thickness, 18);
        EXPECT_NE(material(p, defaults::kParticleboard18)->name, "Local board");
        EXPECT_EQ(p.libraries.materials.size(), before);
        EXPECT_EQ(sideThickness(p, "k1"), 18);
    }
    {   // Rename: 18 mm board added as "k1.lib1"; the item refers to it; identical entries
        // (hdf3, edge bands, built-in hardware) are not duplicated.
        Project p = conflictingTarget();
        auto plan = planComponentInsert(p, c, ConflictPolicy::RenameWithNewId, componentIdGenerator("k1"));
        ASSERT_TRUE(plan.ok());
        EXPECT_EQ(plan.value().count(MergeAction::Renamed), 1u);
        EXPECT_EQ(plan.value().count(MergeAction::Skipped), plan.value().entries.size() - 1);
        const std::size_t hardware = p.libraries.hardware.size();
        ASSERT_TRUE(insertComponent(c, "k1", {}, ConflictPolicy::RenameWithNewId).apply(p).ok());
        EXPECT_EQ(material(p, defaults::kParticleboard18)->thickness, 16);
        ASSERT_NE(material(p, "k1.lib1"), nullptr);
        EXPECT_EQ(material(p, "k1.lib1")->thickness, 18);
        EXPECT_EQ(p.libraries.materials.size(), before + 1);
        EXPECT_EQ(p.libraries.hardware.size(), hardware);
        EXPECT_EQ(p.findItem("k1")->cabinet()->materials.carcass.str(), "k1.lib1");
        EXPECT_EQ(sideThickness(p, "k1"), 18);
    }
}

TEST(Components, MalformedComponentFileIsRefusedWithPath) {
    test::TempDir dir;
    formats::ComponentFile file;
    file.components.push_back(toRecord(baseComponent()));
    file.components[0].library.materials.clear(); // references are no longer embedded
    const auto missing = dir.path / "missing.foscomp";
    ASSERT_TRUE(formats::exportComponentFile(missing, file, {}).ok());
    auto r = loadComponents(missing);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr(missing.string()));
    EXPECT_THAT(r.error().message, HasSubstr("/payload/components/0/library"));

    file.components[0] = toRecord(baseComponent());
    file.components[0].definition["construction"] = "sideways";
    const auto bad = dir.path / "bad.foscomp";
    ASSERT_TRUE(formats::exportComponentFile(bad, file, {}).ok());
    r = loadComponents(bad);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr(bad.string()));
    EXPECT_THAT(r.error().message, HasSubstr("/payload/components/0/definition/construction"));
}

TEST(Components, LibraryIndexesByCategoryAndTagDeterministically) {
    test::TempDir dir;
    Component a = baseComponent();
    Component b = baseComponent();
    b.meta.id = "comp.tall";
    b.meta.name = "Tall";
    b.meta.category = "Tall cabinets";
    b.meta.tags = {"kitchen"};
    Component d = baseComponent();
    d.meta.id = "comp.another";
    d.meta.name = "Another base";
    std::filesystem::create_directories(dir.path / "sub");
    ASSERT_TRUE(saveComponents(dir.path / "sub" / "z.foscomp", {b}).ok());
    ASSERT_TRUE(saveComponents(dir.path / "a.foscomp", {a, d}).ok());
    std::ofstream(dir.path / "broken.foscomp") << "nope";
    ComponentLibrary lib;
    const auto diags = lib.scan({dir.path});
    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].code, "project.components.load_failed");
    EXPECT_THAT(diags[0].message, HasSubstr("broken.foscomp"));
    ASSERT_EQ(lib.entries().size(), 3u);
    // Sorted by (category, name): Base cabinets/Another base, Base cabinets/Base 600, Tall cabinets/Tall.
    EXPECT_EQ(lib.entries()[0].component.meta.name, "Another base");
    EXPECT_EQ(lib.entries()[1].component.meta.name, "Base 600");
    EXPECT_EQ(lib.entries()[2].component.meta.name, "Tall");
    EXPECT_EQ(lib.categories(), (std::vector<std::string>{"Base cabinets", "Tall cabinets"}));
    EXPECT_EQ(lib.tags(), (std::vector<std::string>{"base", "kitchen"}));
    EXPECT_EQ(lib.byTag("kitchen").size(), 3u);
    EXPECT_EQ(lib.byCategory("Tall cabinets").size(), 1u);
    ASSERT_NE(lib.find("comp.tall"), nullptr);
    EXPECT_FALSE(lib.add(a, "again").ok());
}

} // namespace

// Hanging rails: the section codec writes "hangingRail" only when set (older files unchanged),
// "rail" / "supports" only when not the generic default; both ids count as library references.
TEST(Components, HangingRailCodecRoundTripAndLibraryReferences) {
    furniture::Section plain;
    EXPECT_FALSE(toJson(plain).contains("hangingRail"));

    furniture::Section s;
    s.hangingRail = furniture::HangingRailSpec{};
    s.hangingRail->heightFromTop = 75;
    const auto j = toJson(s);
    ASSERT_TRUE(j.contains("hangingRail"));
    EXPECT_DOUBLE_EQ(j["hangingRail"]["heightFromTop"].get<double>(), 75);
    EXPECT_FALSE(j["hangingRail"].contains("supports"));
    EXPECT_FALSE(j["hangingRail"].contains("rail"));
    auto back = sectionFromJson(j, "/section");
    ASSERT_TRUE(back.ok()) << back.error().message;
    EXPECT_EQ(back.value(), s);

    s.hangingRail->supports = domain::HardwareId("hw.my_support");
    const auto j2 = toJson(s);
    EXPECT_EQ(j2["hangingRail"]["supports"].get<std::string>(), "hw.my_support");
    auto back2 = sectionFromJson(j2, "/section");
    ASSERT_TRUE(back2.ok()) << back2.error().message;
    EXPECT_EQ(back2.value(), s);

    auto bad = j;
    bad["hangingRail"]["heightFromTop"] = -5;
    auto refused = sectionFromJson(bad, "/section");
    ASSERT_FALSE(refused.ok());
    EXPECT_THAT(refused.error().message, HasSubstr("heightFromTop"));

    Item item = test::baseCabinetItem("cab1");
    const auto has = [](const LibraryRefs& r, const char* id) {
        return std::find(r.hardware.begin(), r.hardware.end(), id) != r.hardware.end();
    };
    EXPECT_FALSE(has(libraryReferences(item), hardware::ids::kClothesRailGeneric));
    EXPECT_FALSE(has(libraryReferences(item), hardware::ids::kRailSupportGeneric));
    item.cabinet()->interior.hangingRail = furniture::HangingRailSpec{};
    EXPECT_TRUE(has(libraryReferences(item), hardware::ids::kClothesRailGeneric));
    EXPECT_TRUE(has(libraryReferences(item), hardware::ids::kRailSupportGeneric));
}

// Inner drawers: "innerDrawers" only when set (older files unchanged), "joint" only when not
// Dowel, "handle" only for a Facia; the slide counts as a library reference.
TEST(Components, InnerDrawersCodecRoundTripAndLibraryReferences) {
    EXPECT_FALSE(toJson(furniture::Section{}).contains("innerDrawers"));

    furniture::Section s;
    s.innerDrawers = furniture::InnerDrawerSpec{};
    s.innerDrawers->heights = {furniture::SectionSize::fixed(200), furniture::SectionSize::weight()};
    s.innerDrawers->slide = domain::HardwareId("hw.my_slide");
    const auto j = toJson(s);
    ASSERT_TRUE(j.contains("innerDrawers"));
    EXPECT_EQ(j["innerDrawers"]["front"].get<std::string>(), "Box");
    EXPECT_FALSE(j["innerDrawers"].contains("joint"));
    EXPECT_FALSE(j["innerDrawers"].contains("handle"));
    auto back = sectionFromJson(j, "/section");
    ASSERT_TRUE(back.ok()) << back.error().message;
    EXPECT_EQ(back.value(), s);

    s.innerDrawers->front = furniture::InnerDrawerFront::Facia;
    s.innerDrawers->handle = furniture::InnerFaciaHandle::None;
    s.innerDrawers->joint = furniture::JointFastener::Confirmat;
    const auto j2 = toJson(s);
    EXPECT_EQ(j2["innerDrawers"]["handle"].get<std::string>(), "None");
    EXPECT_EQ(j2["innerDrawers"]["joint"].get<std::string>(), "Confirmat");
    auto back2 = sectionFromJson(j2, "/section");
    ASSERT_TRUE(back2.ok()) << back2.error().message;
    EXPECT_EQ(back2.value(), s);

    auto bad = j2;
    bad["innerDrawers"]["front"] = "Drawer";
    EXPECT_FALSE(sectionFromJson(bad, "/section").ok());

    Item item = test::baseCabinetItem("cab1");
    const auto has = [](const LibraryRefs& r, const char* id) {
        return std::find(r.hardware.begin(), r.hardware.end(), id) != r.hardware.end();
    };
    EXPECT_FALSE(has(libraryReferences(item), "hw.my_slide"));
    item.cabinet()->interior.innerDrawers = s.innerDrawers;
    EXPECT_TRUE(has(libraryReferences(item), "hw.my_slide"));
}
