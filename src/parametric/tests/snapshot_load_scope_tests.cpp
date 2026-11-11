// SPDX-License-Identifier: AGPL-3.0-only
#include <algorithm>

#include "fos/parametric/scope.hpp"
#include "test_support.hpp"

using namespace fos::parametric;
using namespace fos::parametric::test;
using fos::core::ErrorCode;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

TEST(Snapshot, UndoRedoSequence) {
    ParameterGraph g = makeCabinet();
    std::vector<Snapshot> history;
    history.push_back(g.snapshot()); // s0: 800 / 18 / 18 -> 764

    FOS_ASSERT_OK(g.setValue("outer_width", 900));
    history.push_back(g.snapshot()); // s1: 864

    FOS_ASSERT_OK(g.apply(Transaction().setValue("left_thickness", 16).setValue("right_thickness", 16)));
    history.push_back(g.snapshot()); // s2: 868

    FOS_ASSERT_OK(g.defineExpression("half", "inner_width / 2"));
    history.push_back(g.snapshot()); // s3: half = 434

    // Undo three times.
    ChangeSet cs = g.restore(history[2]);
    EXPECT_FALSE(g.contains("half"));
    EXPECT_THAT(cs.removed, ElementsAre("half"));
    EXPECT_DOUBLE_EQ(valueOf(g, "inner_width"), 868.0);

    cs = g.restore(history[1]);
    EXPECT_DOUBLE_EQ(valueOf(g, "inner_width"), 864.0);
    EXPECT_THAT(cs.changed, ElementsAre("inner_width", "left_thickness", "right_thickness"));

    cs = g.restore(history[0]);
    EXPECT_DOUBLE_EQ(valueOf(g, "outer_width"), 800.0);
    EXPECT_DOUBLE_EQ(valueOf(g, "inner_width"), 764.0);
    EXPECT_THAT(cs.changed, ElementsAre("inner_width", "outer_width"));

    // Redo to the end.
    cs = g.restore(history[3]);
    EXPECT_DOUBLE_EQ(valueOf(g, "half"), 434.0);
    EXPECT_THAT(cs.added, ElementsAre("half"));
    EXPECT_THAT(cs.changed, ElementsAre("half", "inner_width", "left_thickness", "outer_width", "right_thickness"));

    // Undo once, then branch: editing after an undo must not disturb any snapshot.
    g.restore(history[2]);
    FOS_ASSERT_OK(g.setValue("outer_width", 500));
    EXPECT_DOUBLE_EQ(valueOf(g, "inner_width"), 468.0); // 500 - 32
    g.restore(history[3]);
    EXPECT_DOUBLE_EQ(valueOf(g, "outer_width"), 900.0);
    EXPECT_DOUBLE_EQ(valueOf(g, "half"), 434.0);
    g.restore(history[0]);
    EXPECT_DOUBLE_EQ(valueOf(g, "inner_width"), 764.0);
    EXPECT_THAT(g.dependents("inner_width"), IsEmpty());
    // The restored graph is fully functional (dependency index intact).
    FOS_ASSERT_OK(g.setValue("left_thickness", 20));
    EXPECT_DOUBLE_EQ(valueOf(g, "inner_width"), 762.0); // 800 - 20 - 18
}

TEST(Snapshot, RestoreNotifiesObserversAndInvalidSnapshotIsIgnored) {
    ParameterGraph g = makeCabinet();
    const Snapshot s = g.snapshot();
    FOS_ASSERT_OK(g.setValue("outer_width", 900));
    int calls = 0;
    g.addObserver([&](const ChangeSet& cs) {
        ++calls;
        EXPECT_THAT(cs.changed, ElementsAre("inner_width", "outer_width"));
    });
    g.restore(s);
    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(Snapshot().valid());
    EXPECT_TRUE(g.restore(Snapshot()).empty());
    EXPECT_EQ(calls, 1);
    EXPECT_DOUBLE_EQ(valueOf(g, "inner_width"), 764.0);
}

TEST(Snapshot, CopiesAreIndependent) {
    ParameterGraph a = makeCabinet();
    ParameterGraph b = a; // shares state until one of them changes
    FOS_ASSERT_OK(b.setValue("outer_width", 600));
    EXPECT_DOUBLE_EQ(valueOf(a, "inner_width"), 764.0);
    EXPECT_DOUBLE_EQ(valueOf(b, "inner_width"), 564.0);
    FOS_ASSERT_OK(a.remove("inner_width"));
    EXPECT_TRUE(b.contains("inner_width"));

    ParameterGraph moved = std::move(b);
    EXPECT_DOUBLE_EQ(valueOf(moved, "inner_width"), 564.0);
    // A moved-from graph is an empty, usable graph.
    EXPECT_EQ(b.size(), 0u); // NOLINT(bugprone-use-after-move)
    FOS_ASSERT_OK(b.defineConstant("x", 1));
}

TEST(BulkLoad, RoundTripThroughDefinitionRecords) {
    ParameterGraph g = makeCabinet();
    ParameterDef count = ParameterDef::constant("shelf_count", 3, ParameterKind::Count);
    count.integer = true;
    count.lowerBound = 0;
    count.upperBound = 12;
    count.description = "Number of adjustable shelves";
    FOS_ASSERT_OK(g.define(count));
    FOS_ASSERT_OK(g.addConstraint({"fits", "inner_width > 100", "Too narrow: {inner_width}", ConstraintSeverity::Warning}));

    std::vector<ParameterDef> defs = g.definitions();
    const std::vector<ConstraintDef> constraints = g.constraints();
    // Any order must work: reverse puts `inner_width` before the parameters it depends on... and
    // also try expression-first ordering explicitly.
    std::reverse(defs.begin(), defs.end());
    std::stable_partition(defs.begin(), defs.end(), [](const ParameterDef& d) { return d.isExpression(); });
    ASSERT_EQ(defs.front().name, "inner_width");

    auto loaded = ParameterGraph::load(defs, constraints);
    FOS_ASSERT_OK(loaded);
    const ParameterGraph& h = loaded.value();
    const auto a = g.definitions();
    const auto b = h.definitions();
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].name, b[i].name);
        EXPECT_EQ(a[i].expression, b[i].expression);
        EXPECT_EQ(a[i].value, b[i].value);
        EXPECT_EQ(a[i].kind, b[i].kind);
        EXPECT_EQ(a[i].description, b[i].description);
        EXPECT_EQ(a[i].lowerBound, b[i].lowerBound);
        EXPECT_EQ(a[i].upperBound, b[i].upperBound);
        EXPECT_EQ(a[i].integer, b[i].integer);
    }
    ASSERT_EQ(h.constraints().size(), 1u);
    EXPECT_EQ(h.constraints()[0].message, "Too narrow: {inner_width}");
    EXPECT_EQ(h.constraints()[0].severity, ConstraintSeverity::Warning);
    EXPECT_DOUBLE_EQ(valueOf(h, "inner_width"), 764.0);
    EXPECT_THAT(h.dependents("outer_width"), ElementsAre("inner_width"));
}

TEST(BulkLoad, StaleStoredValuesOfExpressionsAreRecomputed) {
    ParameterDef inner = ParameterDef::formula("inner", "w - 36");
    inner.value = 123456; // a stale cached value in a file must not be trusted
    auto loaded = ParameterGraph::load({inner, ParameterDef::constant("w", 800)});
    FOS_ASSERT_OK(loaded);
    EXPECT_DOUBLE_EQ(valueOf(loaded.value(), "inner"), 764.0);
}

TEST(BulkLoad, RejectsCyclesUndefinedReferencesAndDuplicates) {
    auto cyclic = ParameterGraph::load({ParameterDef::formula("a", "b + 1"), ParameterDef::formula("b", "c + 1"),
                                        ParameterDef::formula("c", "a + 1"), ParameterDef::constant("d", 1)});
    ASSERT_FALSE(cyclic.ok());
    EXPECT_EQ(cyclic.error().code, ErrorCode::CyclicDependency);
    EXPECT_THAT(cyclic.error().message, HasSubstr("a -> b -> c -> a"));

    auto missing = ParameterGraph::load({ParameterDef::formula("a", "b + 1")});
    ASSERT_FALSE(missing.ok());
    EXPECT_THAT(missing.error().message, HasSubstr("'a' refers to undefined parameter 'b'"));

    auto duplicate = ParameterGraph::load({ParameterDef::constant("a", 1), ParameterDef::constant("a", 2)});
    ASSERT_FALSE(duplicate.ok());
    EXPECT_EQ(duplicate.error().code, ErrorCode::AlreadyExists);

    auto violated = ParameterGraph::load({ParameterDef::constant("a", 1)},
                                         {{"positive", "a > 5", "a too small", ConstraintSeverity::Error}});
    ASSERT_FALSE(violated.ok());
    EXPECT_EQ(violated.error().code, ErrorCode::ConstraintViolation);

    auto empty = ParameterGraph::load({});
    FOS_ASSERT_OK(empty);
    EXPECT_EQ(empty.value().size(), 0u);
}

namespace {

/// Shelf written with local names; `outer.inner_width` is the enclosing cabinet's inner width.
ParameterTemplate shelfTemplate() {
    ParameterTemplate t;
    t.parameters.push_back(ParameterDef::constant("gap", 1, ParameterKind::Length));
    t.parameters.push_back(ParameterDef::formula("length", "outer.inner_width - 2 * gap", ParameterKind::Length));
    t.parameters.push_back(ParameterDef::formula("area", "length * global_depth"));
    t.constraints.push_back({"long enough", "length > 100", "Shelf is only {length} mm of {outer.inner_width} mm",
                             ConstraintSeverity::Error});
    return t;
}

ParameterTemplate cabinetTemplate() {
    ParameterTemplate t;
    t.parameters.push_back(ParameterDef::constant("width", 800, ParameterKind::Length));
    t.parameters.push_back(ParameterDef::constant("t", 18, ParameterKind::Length));
    t.parameters.push_back(ParameterDef::formula("inner_width", "width - 2 * t", ParameterKind::Length));
    auto shelf = instantiate(shelfTemplate(), "shelf");
    EXPECT_TRUE(shelf.ok());
    return merge(t, shelf.value());
}

} // namespace

TEST(Scope, QualifyAndSingleLevelInstantiate) {
    EXPECT_EQ(qualify("cab1", "width"), "cab1.width");
    EXPECT_EQ(qualify("", "width"), "width");

    auto shelf = instantiate(shelfTemplate(), "shelf");
    FOS_ASSERT_OK(shelf);
    ASSERT_EQ(shelf.value().parameters.size(), 3u);
    EXPECT_EQ(shelf.value().parameters[0].name, "shelf.gap");
    EXPECT_EQ(shelf.value().parameters[1].name, "shelf.length");
    // Local `gap` is prefixed, `outer.` is stripped, the kind is kept.
    EXPECT_EQ(shelf.value().parameters[1].expression, "inner_width - 2 * shelf.gap");
    EXPECT_EQ(shelf.value().parameters[1].kind, ParameterKind::Length);
    // `global_depth` is not part of the template: absolute reference, unchanged.
    EXPECT_EQ(shelf.value().parameters[2].expression, "shelf.length * global_depth");
    EXPECT_EQ(shelf.value().constraints[0].name, "shelf.long enough");
    EXPECT_EQ(shelf.value().constraints[0].expression, "shelf.length > 100");
    EXPECT_EQ(shelf.value().constraints[0].message, "Shelf is only {shelf.length} mm of {inner_width} mm");

    EXPECT_EQ(instantiate(shelfTemplate(), "1bad").error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(instantiate(shelfTemplate(), "").error().code, ErrorCode::InvalidArgument);
    ParameterTemplate broken;
    broken.parameters.push_back(ParameterDef::formula("x", "1 +"));
    auto r = instantiate(broken, "p");
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("Parameter 'x'"));
}

TEST(Scope, SameTemplateUnderTwoPrefixesWithNestedScopes) {
    Transaction tx;
    tx.define(ParameterDef::constant("global_depth", 500, ParameterKind::Length));
    for (const char* prefix : {"cab1", "cab2"}) {
        auto cab = instantiate(cabinetTemplate(), prefix);
        FOS_ASSERT_OK(cab);
        appendTo(tx, cab.value());
    }
    ParameterGraph g;
    FOS_ASSERT_OK(g.apply(tx));
    EXPECT_EQ(g.size(), 13u); // 1 global + 2 * (3 cabinet + 3 shelf)
    EXPECT_EQ(g.find("cab1.shelf.length")->expression, "cab1.inner_width - 2 * cab1.shelf.gap");
    EXPECT_EQ(g.find("cab2.shelf.area")->expression, "cab2.shelf.length * global_depth");
    EXPECT_DOUBLE_EQ(valueOf(g, "cab1.shelf.length"), 762.0); // 800 - 36 - 2
    EXPECT_DOUBLE_EQ(valueOf(g, "cab2.shelf.area"), 381000.0); // 762 * 500
    ASSERT_EQ(g.constraints().size(), 2u);
    EXPECT_EQ(g.constraints()[0].name, "cab1.shelf.long enough");

    // The two instances are independent.
    FOS_ASSERT_OK(g.setValue("cab2.width", 600));
    EXPECT_DOUBLE_EQ(valueOf(g, "cab1.shelf.length"), 762.0);
    EXPECT_DOUBLE_EQ(valueOf(g, "cab2.shelf.length"), 562.0); // 600 - 36 - 2
    EXPECT_THAT(g.lastRecalculation().evaluated,
                ElementsAre("cab2.inner_width", "cab2.shelf.length", "cab2.shelf.area"));
    // The shared outer parameter drives both.
    FOS_ASSERT_OK(g.setValue("global_depth", 400));
    EXPECT_DOUBLE_EQ(valueOf(g, "cab1.shelf.area"), 304800.0); // 762 * 400
    EXPECT_DOUBLE_EQ(valueOf(g, "cab2.shelf.area"), 224800.0); // 562 * 400

    // The scoped constraint reports scoped names and values: 130 - 36 - 2 = 92 <= 100.
    auto r = g.setValue("cab1.width", 130);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().message, "Constraint 'cab1.shelf.long enough' violated: Shelf is only 92 mm of 94 mm "
                                 "(cab1.shelf.length > 100 is false; cab1.shelf.length = 92).");
}
