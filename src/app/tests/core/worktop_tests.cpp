// SPDX-License-Identifier: AGPL-3.0-only
// Edit > Add worktop… view-model (worktop_form.hpp) over the project worktop engine.
#include "fos/app/templates.hpp"
#include "fos/app/worktop_form.hpp"
#include "test_support.hpp"

namespace fos::app::test {
namespace {

/// Three 600 mm base cabinets side by side (x = 0, 600, 1200); returns their ids.
std::vector<std::string> kitchen(Session& s) {
    const TemplateSize size{600, 720, 560};
    EXPECT_TRUE(s.newFromTemplate(TemplateKind::BaseCabinet, size).ok());
    std::vector<std::string> ids{s.project().items[0].id()};
    for (int i = 0; i < 2; ++i) {
        auto id = s.addFromTemplate(TemplateKind::BaseCabinet, size);
        EXPECT_TRUE(id.ok());
        if (id) ids.push_back(id.value());
    }
    return ids;
}

} // namespace

TEST(WorktopForm, ChoicesAndPreselection) {
    Session s;
    const auto ids = kitchen(s);
    ASSERT_EQ(ids.size(), 3u);
    // addFromTemplate selected the last cabinet -> only it is checked.
    auto rows = worktopCabinetRows(s.project(), s.selection());
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_FALSE(rows[0].checked);
    EXPECT_TRUE(rows[2].checked);
    EXPECT_EQ(rows[2].id, ids[2]);
    // Nothing selected -> every cabinet.
    rows = worktopCabinetRows(s.project(), Selection{});
    for (const auto& r : rows) EXPECT_TRUE(r.checked);

    const auto materials = worktopMaterialChoices(s.project());
    ASSERT_EQ(materials.size(), s.project().libraries.materials.size() + 1);
    EXPECT_TRUE(materials[0].id.empty()); // generic placeholder
    const auto bands = worktopBandChoices(s.project());
    ASSERT_EQ(bands.size(), s.project().libraries.edgeBands.size() + 1);
    EXPECT_TRUE(bands[0].id.empty()); // automatic
    EXPECT_EQ(worktopCutoutKinds().size(), 2u);

    const auto req = defaultWorktopRequest(s.project(), ids);
    EXPECT_EQ(req.id, "worktop1");
    EXPECT_EQ(req.name, "Worktop");
    EXPECT_EQ(req.frontOverhang, 20.0); // engine default kept
}

TEST(WorktopForm, PreviewSplitsAndApplyIsOneUndoStep) {
    Session s;
    const auto ids = kitchen(s);
    auto req = defaultWorktopRequest(s.project(), ids);
    // Run 0 .. 1800 with no side overhangs = 1800 mm; at most 1000 mm per piece ->
    // ceil(1800 / 1000) = 2 equal pieces of 900, one butt joint at 900 from the left end.
    req.maxPieceLength = 1000;
    const WorktopPreview preview = worktopPreview(s.project(), req);
    ASSERT_TRUE(preview.ok) << preview.error.toStdString();
    EXPECT_EQ(preview.pieceIds, (std::vector<std::string>{"worktop1.1", "worktop1.2"}));
    EXPECT_TRUE(preview.text.contains(QStringLiteral("Length 1800 mm"))) << preview.text.toStdString();
    EXPECT_TRUE(preview.text.contains(QStringLiteral("Butt joints at 900 mm"))) << preview.text.toStdString();
    EXPECT_TRUE(preview.text.contains(QStringLiteral(": 900 mm"))) << preview.text.toStdString();

    // A sink centred 60 mm from the run start: its 400 mm length reaches x = -140, beyond the
    // free left end (minimum 100 mm to the end) -> refused, the engine message is shown.
    auto bad = req;
    bad.cutouts.push_back({project::WorktopCutoutKind::Sink, 60, 400, 400, 0, std::nullopt});
    const WorktopPreview refused = worktopPreview(s.project(), bad);
    EXPECT_FALSE(refused.ok);
    EXPECT_FALSE(refused.error.isEmpty());
    EXPECT_TRUE(refused.text.isEmpty());
    const std::size_t before = s.project().items.size();
    EXPECT_FALSE(applyWorktop(s, bad).ok());
    EXPECT_EQ(s.project().items.size(), before);

    auto added = applyWorktop(s, req);
    ASSERT_TRUE(added.ok()) << added.error().message;
    EXPECT_EQ(added.value(), preview.pieceIds);
    EXPECT_EQ(s.project().items.size(), before + 2);
    EXPECT_EQ(s.selection().itemId, "worktop1.1");
    // The id is now taken (by its pieces) -> the next dialog proposes worktop2.
    EXPECT_EQ(nextWorktopId(s.project()), "worktop2");
    ASSERT_TRUE(s.undo().ok());
    EXPECT_EQ(s.project().items.size(), before);
}

} // namespace fos::app::test
