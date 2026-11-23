// SPDX-License-Identifier: AGPL-3.0-only
// Edit > Add plinth… view-model (plinth_form.hpp) over the project plinth engine.
#include "fos/app/plinth_form.hpp"
#include "fos/app/templates.hpp"
#include "fos/furniture/edit.hpp"
#include "test_support.hpp"

namespace fos::app::test {
namespace {

/// Three 600 mm base cabinets side by side (x = 0, 600, 1200); with `feet` they are switched
/// from the template's SidesToFloor plinth to feet (each switch is one undo step).
std::vector<std::string> kitchen(Session& s, bool feet) {
    const TemplateSize size{600, 720, 560};
    EXPECT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet, size).ok());
    std::vector<std::string> ids{s.project().items[0].id()};
    for (int i = 0; i < 2; ++i) {
        auto id = s.addFromTemplate(TemplateKind::BaseCabinet, size);
        EXPECT_TRUE(id.ok());
        if (id) ids.push_back(id.value());
    }
    if (feet) {
        for (const auto& id : ids) {
            furniture::ConstructionOptions o = s.project().findItem(id)->cabinet()->construction;
            o.plinth = furniture::PlinthKind::Feet;
            EXPECT_TRUE(s.execute(project::commands::setConstruction(id, o)).ok());
        }
    }
    return ids;
}

} // namespace

TEST(PlinthForm, ChoicesDefaultsAndRefusal) {
    Session s;
    const auto ids = kitchen(s, false);
    ASSERT_EQ(ids.size(), 3u);
    // The checklist is shared with the worktop dialog: selected cabinet only.
    const auto rows = cabinetRunRows(s.project(), s.selection());
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_TRUE(rows[2].checked);
    EXPECT_FALSE(rows[0].checked);

    const auto materials = plinthMaterialChoices(s.project());
    ASSERT_EQ(materials.size(), s.project().libraries.materials.size() + 1);
    EXPECT_TRUE(materials[0].id.empty()); // carcass material of the first cabinet

    const auto req = defaultPlinthRequest(s.project(), ids);
    EXPECT_EQ(req.id, "plinth1");
    EXPECT_EQ(req.name, "Plinth");
    EXPECT_EQ(req.setback, 50.0);      // engine default kept
    EXPECT_FALSE(req.height.has_value()); // from the cabinets

    // The template cabinets have sides to the floor (own plinth panel) -> refused, the
    // engine message is shown and applying changes nothing.
    const PlinthPreview refused = plinthPreview(s.project(), req);
    EXPECT_FALSE(refused.ok);
    EXPECT_FALSE(refused.error.isEmpty());
    EXPECT_TRUE(refused.text.isEmpty());
    const std::size_t before = s.project().items.size();
    EXPECT_FALSE(applyPlinth(s, req).ok());
    EXPECT_EQ(s.project().items.size(), before);
}

TEST(PlinthForm, PreviewSplitsAndApplyIsOneUndoStep) {
    Session s;
    const auto ids = kitchen(s, true);
    auto req = defaultPlinthRequest(s.project(), ids);
    // Run 0 .. 1800 (3 x 600); at most 1000 mm per piece -> ceil(1800 / 1000) = 2 equal pieces
    // of 900, one butt joint at 900 from the left end; plus a left return.
    req.maxPieceLength = 1000;
    req.returnLeft = true;
    const PlinthPreview preview = plinthPreview(s.project(), req);
    ASSERT_TRUE(preview.ok) << preview.error.toStdString();
    EXPECT_EQ(preview.itemIds, (std::vector<std::string>{"plinth1.1", "plinth1.2", "plinth1.left"}));
    EXPECT_TRUE(preview.text.contains(QStringLiteral("Length 1800 mm"))) << preview.text.toStdString();
    EXPECT_TRUE(preview.text.contains(QStringLiteral("Front pieces: 2"))) << preview.text.toStdString();
    EXPECT_TRUE(preview.text.contains(QStringLiteral(": 900 mm, from 0 to 900 mm"))) << preview.text.toStdString();
    EXPECT_TRUE(preview.text.contains(QStringLiteral("Butt joints at 900 mm"))) << preview.text.toStdString();

    const std::size_t before = s.project().items.size();
    auto added = applyPlinth(s, req);
    ASSERT_TRUE(added.ok()) << added.error().message;
    EXPECT_EQ(added.value(), preview.itemIds);
    EXPECT_EQ(s.project().items.size(), before + 3);
    EXPECT_EQ(s.selection().itemId, "plinth1.1");
    EXPECT_EQ(nextPlinthId(s.project()), "plinth2");
    ASSERT_TRUE(s.undo().ok()); // one step removes all three items
    EXPECT_EQ(s.project().items.size(), before);
}

} // namespace fos::app::test
