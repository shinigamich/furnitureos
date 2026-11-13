// SPDX-License-Identifier: AGPL-3.0-only
// Hard limits (limits.hpp): enforced by generate() and therefore by every edit; absurd sizes
// are refused; generation of the largest allowed assemblies stays fast.
#include "fos/furniture/limits.hpp"
#include "fos/furniture/panel.hpp"
#include "test_support.hpp"

#include <chrono>

namespace fos::furniture::test {
using ::testing::HasSubstr;

namespace {
std::string refusal(const CabinetDefinition& d) {
    auto r = generate(d, libs());
    return r.ok() ? std::string("(generated)") : r.error().message;
}
Section leaf(int shelves = 0) { return sized(SectionSize::weight(), shelves); }
Section split(SplitDirection dir, std::vector<Section> children) {
    Section s = leaf();
    s.split = dir;
    s.children = std::move(children);
    return s;
}
Section drawerStack(std::size_t drawers) {
    Section s = leaf();
    FrontSpec f;
    f.kind = FrontKind::DrawerStack;
    f.handle = HandlePosition::None;
    f.drawerHeights.assign(drawers, SectionSize::weight());
    s.front = f;
    return s;
}
double seconds(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}
} // namespace

TEST(Limits, ShelvesPerSectionAreLimitedInGenerateAndEdits) {
    const Libraries l = libs();
    CabinetDefinition d = defaultCabinet();
    ASSERT_TRUE(setParameter(d, l, "height", 5000).ok());
    // 100 shelves fit: interior 5000 - 36 = 4964 mm, 100 * 18 = 1800 mm of boards.
    auto ok = setShelfCount(d, l, "", limits::kMaxShelvesPerSection);
    ASSERT_TRUE(ok.ok()) << ok.error().message;
    EXPECT_TRUE(has(ok.value().generated.assembly, "shelf.100"));
    // 101: refused by the edit (definition unchanged) and by generate().
    auto refused = setShelfCount(d, l, "", 101);
    ASSERT_FALSE(refused.ok());
    EXPECT_EQ(refused.error().code, core::ErrorCode::ConstraintViolation);
    EXPECT_THAT(refused.error().message,
                HasSubstr("Section 'root' of cabinet 'cab' has 101 adjustable shelves; the limit is 100 per section."));
    EXPECT_EQ(d.interior.shelfCount, 100);
    CabinetDefinition raw = d;
    raw.interior.shelfCount = 2000; // the reviewer's file: used to take 11 s, now refused at once
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_THAT(refusal(raw), HasSubstr("has 2000 adjustable shelves"));
    EXPECT_LT(seconds(t0), 1.0);
}

TEST(Limits, ShelvesPerCabinetChildrenSectionsDepthAndDrawers) {
    CabinetDefinition d = defaultCabinet();
    // 3 columns of 100 shelves = 300 > 200; the third column reaches the limit (100 + 100 + 100).
    d.interior = split(SplitDirection::Vertical, {leaf(100), leaf(100), leaf(100)});
    EXPECT_THAT(refusal(d), HasSubstr("hold more than 200 adjustable shelves together (limit 200 per cabinet; reached "
                                      "at section '3')"));

    d.interior = split(SplitDirection::Vertical, std::vector<Section>(51, leaf()));
    EXPECT_THAT(refusal(d), HasSubstr("Section 'root' of cabinet 'cab' is split into 51 child sections; the limit is 50."));

    // 1 + 50 + 50 * 5 = 301 sections > 250.
    d.interior = split(SplitDirection::Horizontal,
                       std::vector<Section>(50, split(SplitDirection::Vertical, std::vector<Section>(5, leaf()))));
    EXPECT_THAT(refusal(d), HasSubstr("has more than 250 sections (limit 250)"));

    // A chain 17 levels deep: every level a split into 2, the first child split again.
    Section chain = leaf();
    for (int i = 0; i < 17; ++i) chain = split(SplitDirection::Horizontal, {chain, leaf()});
    d.interior = chain;
    // The deepest node is the leaf below 17 splits: path "1.1. ... .1" (17 ones) at depth 17.
    EXPECT_THAT(refusal(d), HasSubstr("is nested 17 levels deep; the limit is 16 levels."));

    d.interior = drawerStack(31);
    EXPECT_THAT(refusal(d), HasSubstr("has a drawer stack of 31 drawer fronts; the limit is 30."));

    // 50 columns, each split into 2 rows of 30-drawer stacks: 151 sections (allowed), but
    // 8 carcass + (49 + 50 * 1) dividers + 100 * 30 fronts = 3107 estimated parts > 2000.
    d.interior = split(SplitDirection::Vertical, std::vector<Section>(50, split(SplitDirection::Horizontal,
                                                                                 {drawerStack(30), drawerStack(30)})));
    EXPECT_EQ(estimatePartCount(d.interior), 3107u);
    EXPECT_THAT(refusal(d), HasSubstr("would generate up to 3107 parts; the limit is 2000 parts per assembly."));
}

TEST(Limits, DimensionsAndSizesAreLimited) {
    const Libraries l = libs();
    CabinetDefinition d = defaultCabinet();
    // The reviewer's width = 1e308 used to yield parts of infinite length.
    for (const double w : {1e308, 100001.0}) {
        auto r = setParameter(d, l, "width", w);
        ASSERT_FALSE(r.ok()) << w;
        EXPECT_THAT(r.error().message, HasSubstr("Parameter 'width' of cabinet 'cab' is "));
        EXPECT_THAT(r.error().message, HasSubstr("lengths are limited to 100000 mm"));
    }
    EXPECT_THAT(setParameter(d, l, "width", 1e308).error().message, HasSubstr("is 1e+308 mm"));
    EXPECT_EQ(d.parameters.value("width").value(), 600.0); // unchanged
    // A length exactly at the limit is a valid (if absurd) cabinet: top 100000 - 36 mm long.
    auto atLimit = setParameter(d, l, "width", limits::kMaxDimension);
    ASSERT_TRUE(atLimit.ok()) << atLimit.error().message;
    EXPECT_NEAR(part(atLimit.value().generated.assembly, "top").length, 100000 - 36, 1e-9);

    // A Fixed size beyond the limit, a huge weight and an Expression size over a huge custom
    // parameter (custom parameters themselves are not limited).
    CabinetDefinition s = defaultCabinet();
    s.interior = split(SplitDirection::Vertical, {sized(SectionSize::fixed(2e5)), leaf()});
    EXPECT_THAT(refusal(s), HasSubstr("Section '1' of cabinet 'cab' has a fixed size of 200000 mm; the limit is 100000 mm."));
    s.interior = split(SplitDirection::Vertical, {sized(SectionSize::weight(1e300)), leaf()});
    EXPECT_THAT(refusal(s), HasSubstr("has a weight of 1e+300; the limit is 1000000."));
    s.interior = leaf();
    parametric::ParameterDef big;
    big.name = "big";
    big.value = 1e300;
    ASSERT_TRUE(addParameter(s, l, big).ok());
    s.interior = split(SplitDirection::Vertical, {sized(SectionSize::expr("big")), leaf()});
    EXPECT_THAT(refusal(s), HasSubstr("the size is 1e+300 mm; sizes are limited to 100000 mm."));
}

TEST(Limits, NonPositivePartSizesAreInvalidSizes) {
    // top_length pinned to -5 mm is within the magnitude limit and no standard constraint
    // covers it, but a top -5 mm long is not a part: furniture.geometry.invalid_size, before
    // any hardware is placed on it. (Most other sizes are guarded by "std." constraints.)
    const Libraries l = libs();
    CabinetDefinition d = defaultCabinet();
    auto r = setParameter(d, l, "top_length", -5);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::InvalidArgument);
    // Top: top_length x depth 560 x thickness 18.
    EXPECT_THAT(r.error().message, HasSubstr("furniture.geometry.invalid_size: part 'Top' (cab/top) would be -5 x 560 x "
                                             "18 mm; every part dimension must be finite, greater than 0 and at most "
                                             "100000 mm."));
    EXPECT_FALSE(d.pinnedParameters.count("top_length")); // edit not applied
}

TEST(Limits, PanelParametersAreLimited) {
    PanelDefinition p = makePanel(domain::PartId("p/panel"), "Panel", domain::MaterialId("mat.pb18"));
    ASSERT_TRUE(generatePanel(p, libs()).ok());
    ASSERT_TRUE(p.parameters.setValue(panel_param::kLength, 1e308).ok());
    auto r = generatePanel(p, libs());
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("furniture.geometry.invalid_size: parameter 'length' is "));
    EXPECT_THAT(r.error().message, HasSubstr("limited to 100000 mm"));
    ASSERT_TRUE(p.parameters.setValue(panel_param::kLength, 600).ok());
    ASSERT_TRUE(p.parameters.setValue(panel_param::kX, -2e5).ok()); // positions too
    EXPECT_FALSE(generatePanel(p, libs()).ok());
}

TEST(Limits, LargestAllowedAssembliesGenerateQuickly) {
    // (a) 50 columns of 30-drawer stacks: 2 sides + bottom + top + back + 49 partitions +
    // 1500 drawer fronts = 1554 parts (estimate 8 + 49 + 1500 = 1557 >= actual).
    CabinetDefinition d = defaultCabinet();
    parametric::Transaction tx;
    tx.setValue("width", 12000).setValue("height", 2400);
    ASSERT_TRUE(d.parameters.apply(tx).ok());
    d.interior = split(SplitDirection::Vertical, std::vector<Section>(50, drawerStack(30)));
    EXPECT_EQ(estimatePartCount(d.interior), 1557u);
    auto t0 = std::chrono::steady_clock::now();
    auto r = generate(d, libs());
    const double drawers = seconds(t0);
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().assembly.parts.size(), 1554u);
    EXPECT_THAT(errorsOf(r.value().diagnostics), ::testing::IsEmpty());
    EXPECT_TRUE(has(r.value().assembly, "drawer.50.30"));
    // Generous bound for unoptimised builds (measured ~0.05 s); the old linear slot lookup
    // and the assembly-wide scans of hardware::applyPlacement made this quadratic.
    EXPECT_LT(drawers, 5.0);

    // (b) 200 shelves (the cabinet limit) stacked against the same two sides: 2 rows of 100.
    // Each side receives 2 pins per shelf: 400 shelf-pin holes + 2 joints.
    CabinetDefinition s = defaultCabinet();
    parametric::Transaction ts;
    ts.setValue("height", 9000);
    ASSERT_TRUE(s.parameters.apply(ts).ok());
    s.interior = split(SplitDirection::Horizontal, {leaf(100), leaf(100)});
    t0 = std::chrono::steady_clock::now();
    auto rs = generate(s, libs());
    const double shelves = seconds(t0);
    ASSERT_TRUE(rs.ok()) << rs.error().message;
    EXPECT_THAT(errorsOf(rs.value().diagnostics), ::testing::IsEmpty());
    std::size_t pinHoles = 0;
    for (const auto& op : part(rs.value().assembly, "side.left").operations)
        if (op.source.rfind("cab/hw.pins.", 0) == 0) ++pinHoles;
    EXPECT_EQ(pinHoles, 400u);
    EXPECT_LT(shelves, 5.0);
}

TEST(Limits, StatsAndEstimateAreExact) {
    // root V split into [leaf 3 shelves + door, H split into (leaf, door pair leaf)]:
    // sections 5, depth 2, shelves 3, dividers 1 + 1, fronts 1 + 2.
    Section a = leaf(3);
    a.front = FrontSpec{};
    Section pair = leaf();
    pair.front = FrontSpec{};
    pair.front->kind = FrontKind::DoorPair;
    const Section root = split(SplitDirection::Vertical, {a, split(SplitDirection::Horizontal, {leaf(), pair})});
    const SectionTreeStats st = sectionTreeStats(root);
    EXPECT_EQ(st.sections, 5u);
    EXPECT_EQ(st.maxDepth, 2u);
    EXPECT_EQ(st.shelves, 3u);
    EXPECT_EQ(st.dividers, 2u);
    EXPECT_EQ(st.fronts, 3u);
    EXPECT_EQ(estimatePartCount(root), 8u + 2u + 3u + 3u);
    EXPECT_TRUE(checkSectionLimits(root, "x").ok());
}

} // namespace fos::furniture::test
