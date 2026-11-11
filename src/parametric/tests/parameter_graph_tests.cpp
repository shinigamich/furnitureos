// SPDX-License-Identifier: AGPL-3.0-only
#include <algorithm>
#include <limits>

#include "test_support.hpp"

using namespace fos::parametric;
using namespace fos::parametric::test;
using fos::core::ErrorCode;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

TEST(ParameterGraph, CanonicalInnerWidthExample) {
    ParameterGraph g = makeCabinet();
    EXPECT_DOUBLE_EQ(valueOf(g, "inner_width"), 764.0); // 800 - 18 - 18

    auto r = g.setValue("outer_width", 900);
    FOS_ASSERT_OK(r);
    EXPECT_DOUBLE_EQ(valueOf(g, "inner_width"), 864.0); // 900 - 18 - 18
    EXPECT_THAT(r.value().changed, ElementsAre("inner_width", "outer_width"));
    EXPECT_THAT(g.lastRecalculation().evaluated, ElementsAre("inner_width"));

    r = g.apply(Transaction().setValue("left_thickness", 16).setValue("right_thickness", 16));
    FOS_ASSERT_OK(r);
    EXPECT_DOUBLE_EQ(valueOf(g, "inner_width"), 868.0); // 900 - 16 - 16
    EXPECT_THAT(r.value().changed, ElementsAre("inner_width", "left_thickness", "right_thickness"));
    EXPECT_THAT(g.lastRecalculation().evaluated, ElementsAre("inner_width")); // once, not twice
}

TEST(ParameterGraph, MultiLevelDependentsRecalculateInOrderAndOnlyWhenAffected) {
    ParameterGraph g;
    FOS_ASSERT_OK(g.defineConstant("outer_width", 800));
    FOS_ASSERT_OK(g.defineConstant("t", 18));
    FOS_ASSERT_OK(g.defineConstant("depth", 560));
    FOS_ASSERT_OK(g.defineConstant("shelf_count", 3));
    FOS_ASSERT_OK(g.defineExpression("inner", "outer_width - 2 * t"));       // 800 - 36 = 764
    FOS_ASSERT_OK(g.defineExpression("shelf_len", "inner - 2"));             // 762
    FOS_ASSERT_OK(g.defineExpression("total", "shelf_len * shelf_count"));   // 762 * 3 = 2286
    FOS_ASSERT_OK(g.defineExpression("shelf_depth", "depth - 20"));          // 540
    EXPECT_DOUBLE_EQ(valueOf(g, "total"), 2286.0);
    EXPECT_DOUBLE_EQ(valueOf(g, "shelf_depth"), 540.0);

    auto r = g.setValue("t", 16);
    FOS_ASSERT_OK(r);
    EXPECT_DOUBLE_EQ(valueOf(g, "inner"), 768.0);     // 800 - 32
    EXPECT_DOUBLE_EQ(valueOf(g, "shelf_len"), 766.0); // 768 - 2
    EXPECT_DOUBLE_EQ(valueOf(g, "total"), 2298.0);    // 766 * 3
    // Exactly the transitive dependents, dependencies first; shelf_depth was not recomputed.
    EXPECT_THAT(g.lastRecalculation().evaluated, ElementsAre("inner", "shelf_len", "total"));
    EXPECT_THAT(g.lastRecalculation().changed, ElementsAre("t", "inner", "shelf_len", "total"));
    EXPECT_EQ(g.lastRecalculation().visited, 4u);
    EXPECT_THAT(r.value().changed, ElementsAre("inner", "shelf_len", "t", "total"));

    FOS_ASSERT_OK(g.setValue("shelf_count", 4));
    EXPECT_DOUBLE_EQ(valueOf(g, "total"), 3064.0); // 766 * 4
    EXPECT_THAT(g.lastRecalculation().evaluated, ElementsAre("total"));
}

TEST(ParameterGraph, DiamondEvaluatesEachNodeOnce) {
    ParameterGraph g;
    FOS_ASSERT_OK(g.defineConstant("a", 1));
    FOS_ASSERT_OK(g.defineExpression("b", "a + 1")); // 2
    FOS_ASSERT_OK(g.defineExpression("c", "a * 2")); // 2
    FOS_ASSERT_OK(g.defineExpression("d", "b + c")); // 4
    EXPECT_DOUBLE_EQ(valueOf(g, "d"), 4.0);
    FOS_ASSERT_OK(g.setValue("a", 3));
    EXPECT_DOUBLE_EQ(valueOf(g, "d"), 10.0); // (3 + 1) + (3 * 2)
    const auto& evaluated = g.lastRecalculation().evaluated;
    ASSERT_EQ(evaluated.size(), 3u);
    EXPECT_EQ(evaluated.back(), "d"); // after both b and c
}

TEST(ParameterGraph, EarlyCutOffStopsPropagationWhenValueIsUnchanged) {
    ParameterGraph g;
    FOS_ASSERT_OK(g.defineConstant("a", 5));
    FOS_ASSERT_OK(g.defineExpression("b", "min(a, 10)")); // 5
    FOS_ASSERT_OK(g.defineExpression("c", "b * 2"));      // 10
    FOS_ASSERT_OK(g.defineExpression("d", "c + 1"));      // 11
    FOS_ASSERT_OK(g.setValue("a", 12));
    EXPECT_DOUBLE_EQ(valueOf(g, "b"), 10.0);
    EXPECT_DOUBLE_EQ(valueOf(g, "d"), 21.0); // 10 * 2 + 1
    EXPECT_THAT(g.lastRecalculation().evaluated, ElementsAre("b", "c", "d"));

    // a: 12 -> 20, but b = min(20, 10) stays 10: c and d must not be re-evaluated.
    auto r = g.setValue("a", 20);
    FOS_ASSERT_OK(r);
    EXPECT_THAT(g.lastRecalculation().evaluated, ElementsAre("b"));
    EXPECT_THAT(g.lastRecalculation().changed, ElementsAre("a"));
    EXPECT_THAT(r.value().changed, ElementsAre("a"));
    EXPECT_DOUBLE_EQ(valueOf(g, "d"), 21.0);

    // Setting the same value again changes nothing and evaluates nothing.
    r = g.setValue("a", 20);
    FOS_ASSERT_OK(r);
    EXPECT_TRUE(r.value().empty());
    EXPECT_THAT(g.lastRecalculation().evaluated, IsEmpty());
}

TEST(ParameterGraph, SelfReferenceIsRejected) {
    ParameterGraph g;
    auto r = g.defineExpression("a", "a + 1");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::CyclicDependency);
    EXPECT_THAT(r.error().message, HasSubstr("a -> a"));
    EXPECT_FALSE(g.contains("a"));
    EXPECT_EQ(g.size(), 0u);
    ASSERT_EQ(g.lastDiagnostics().size(), 1u);
    EXPECT_EQ(g.lastDiagnostics()[0].code, "parametric.dependency.cycle");
}

TEST(ParameterGraph, DirectCycleIsRejectedAndGraphUnchanged) {
    ParameterGraph g;
    FOS_ASSERT_OK(g.defineConstant("a", 1));
    FOS_ASSERT_OK(g.defineExpression("b", "a + 1"));
    auto r = g.setExpression("a", "b * 2");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::CyclicDependency);
    EXPECT_THAT(r.error().message, HasSubstr("a -> b -> a"));
    EXPECT_FALSE(g.find("a")->isExpression());
    EXPECT_DOUBLE_EQ(valueOf(g, "a"), 1.0);
    EXPECT_DOUBLE_EQ(valueOf(g, "b"), 2.0);
    EXPECT_THAT(g.dependencies("a"), IsEmpty());
    EXPECT_THAT(g.dependents("b"), IsEmpty());
    EXPECT_THAT(g.dependents("a"), ElementsAre("b"));
}

TEST(ParameterGraph, IndirectCycleListsThePath) {
    ParameterGraph g;
    FOS_ASSERT_OK(g.defineConstant("a", 1));
    FOS_ASSERT_OK(g.defineExpression("b", "a + 1"));
    FOS_ASSERT_OK(g.defineExpression("c", "b + 1"));
    FOS_ASSERT_OK(g.defineExpression("unrelated", "c * 2"));
    // a would depend on c, c depends on b, b depends on a.
    auto r = g.setExpression("a", "c + 1");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::CyclicDependency);
    EXPECT_THAT(r.error().message, HasSubstr("a -> c -> b -> a"));
    EXPECT_THAT(r.error().message, HasSubstr("'c + 1'"));
    EXPECT_DOUBLE_EQ(valueOf(g, "c"), 3.0);
    EXPECT_DOUBLE_EQ(valueOf(g, "unrelated"), 6.0);
    // The graph still works afterwards.
    FOS_ASSERT_OK(g.setValue("a", 10));
    EXPECT_DOUBLE_EQ(valueOf(g, "unrelated"), 24.0); // (10 + 1 + 1) * 2
}

TEST(ParameterGraph, UndefinedReferenceIsRejectedWithTheMissingName) {
    ParameterGraph g;
    FOS_ASSERT_OK(g.defineConstant("width", 800));
    auto r = g.defineExpression("inner", "width - 2 * board.thickness");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::NotFound);
    EXPECT_THAT(r.error().message, HasSubstr("'inner' refers to undefined parameter 'board.thickness'"));
    EXPECT_EQ(r.error().context, "board.thickness");
    EXPECT_FALSE(g.contains("inner"));
    EXPECT_THAT(g.dependents("width"), IsEmpty());
    EXPECT_THAT(g.dependents("board.thickness"), IsEmpty());

    r = g.setExpression("width", "nope + 1");
    ASSERT_FALSE(r.ok());
    EXPECT_DOUBLE_EQ(valueOf(g, "width"), 800.0);
    EXPECT_FALSE(g.find("width")->isExpression());
}

TEST(ParameterGraph, RemoveWithDependentsIsRejectedNamingThem) {
    ParameterGraph g = makeCabinet();
    FOS_ASSERT_OK(g.defineExpression("half", "outer_width / 2"));
    auto r = g.remove("outer_width");
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message,
                HasSubstr("Cannot remove parameter 'outer_width': it is used by 'half', 'inner_width'."));
    EXPECT_TRUE(g.contains("outer_width"));
    EXPECT_DOUBLE_EQ(valueOf(g, "half"), 400.0);

    r = g.remove("half");
    FOS_ASSERT_OK(r);
    EXPECT_THAT(r.value().removed, ElementsAre("half"));
    EXPECT_FALSE(g.contains("half"));
    EXPECT_THAT(g.dependents("outer_width"), ElementsAre("inner_width"));

    EXPECT_EQ(g.remove("half").error().code, ErrorCode::NotFound);
    // Removing a parameter together with all its users in one transaction is fine.
    FOS_ASSERT_OK(g.apply(Transaction().remove("outer_width").remove("inner_width")));
    EXPECT_THAT(g.names(), ElementsAre("left_thickness", "right_thickness"));
}

TEST(ParameterGraph, RenameRewritesDependents) {
    ParameterGraph g = makeCabinet();
    FOS_ASSERT_OK(g.addConstraint({"positive", "outer_width>0", "Width {outer_width} must be positive",
                                   ConstraintSeverity::Error}));
    auto r = g.rename("outer_width", "cab.width");
    FOS_ASSERT_OK(r);
    EXPECT_THAT(r.value().renamed, ElementsAre(std::make_pair(std::string("outer_width"), std::string("cab.width"))));
    EXPECT_THAT(r.value().changed, IsEmpty()); // no value changed
    EXPECT_THAT(r.value().added, IsEmpty());
    EXPECT_THAT(r.value().removed, IsEmpty());
    EXPECT_FALSE(g.contains("outer_width"));
    EXPECT_DOUBLE_EQ(valueOf(g, "cab.width"), 800.0);
    EXPECT_EQ(g.find("cab.width")->kind, ParameterKind::Length);
    EXPECT_EQ(g.find("inner_width")->expression, "cab.width - left_thickness - right_thickness");
    EXPECT_THAT(g.dependencies("inner_width"), ElementsAre("cab.width", "left_thickness", "right_thickness"));
    EXPECT_THAT(g.dependents("cab.width"), ElementsAre("inner_width"));
    EXPECT_EQ(g.constraints()[0].expression, "cab.width > 0");
    EXPECT_EQ(g.constraints()[0].message, "Width {cab.width} must be positive");

    FOS_ASSERT_OK(g.setValue("cab.width", 600));
    EXPECT_DOUBLE_EQ(valueOf(g, "inner_width"), 564.0); // 600 - 36

    EXPECT_EQ(g.rename("cab.width", "inner_width").error().code, ErrorCode::AlreadyExists);
    EXPECT_EQ(g.rename("missing", "x").error().code, ErrorCode::NotFound);
    EXPECT_EQ(g.rename("cab.width", "2wide").error().code, ErrorCode::InvalidArgument);
    EXPECT_TRUE(g.contains("cab.width"));
}

TEST(ParameterGraph, RedefineBetweenConstantAndExpression) {
    ParameterGraph g = makeCabinet();
    // Expression -> constant.
    FOS_ASSERT_OK(g.setValue("inner_width", 500));
    EXPECT_FALSE(g.find("inner_width")->isExpression());
    EXPECT_THAT(g.dependencies("inner_width"), IsEmpty());
    EXPECT_THAT(g.dependents("outer_width"), IsEmpty());
    FOS_ASSERT_OK(g.setValue("outer_width", 1000));
    EXPECT_DOUBLE_EQ(valueOf(g, "inner_width"), 500.0);
    EXPECT_EQ(g.find("inner_width")->kind, ParameterKind::Length); // metadata kept
    // Constant -> expression.
    FOS_ASSERT_OK(g.setExpression("inner_width", "outer_width - 2 * left_thickness"));
    EXPECT_DOUBLE_EQ(valueOf(g, "inner_width"), 964.0); // 1000 - 36
    // Full redefinition replaces metadata too.
    ParameterDef def = ParameterDef::formula("inner_width", "outer_width / 2", ParameterKind::Number);
    def.description = "Half width";
    FOS_ASSERT_OK(g.redefine(def));
    EXPECT_DOUBLE_EQ(valueOf(g, "inner_width"), 500.0);
    EXPECT_EQ(g.find("inner_width")->description, "Half width");
    EXPECT_EQ(g.find("inner_width")->kind, ParameterKind::Number);

    EXPECT_EQ(g.define(def).error().code, ErrorCode::AlreadyExists);
    def.name = "other";
    EXPECT_EQ(g.redefine(def).error().code, ErrorCode::NotFound);
    EXPECT_EQ(g.setValue("other", 1).error().code, ErrorCode::NotFound);
    EXPECT_EQ(g.setExpression("outer_width", "").error().code, ErrorCode::ParseError);
}

TEST(ParameterGraph, ParseErrorNamesParameterAndPosition) {
    ParameterGraph g = makeCabinet();
    auto r = g.setExpression("inner_width", "outer_width - * 2");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::ParseError);
    EXPECT_THAT(r.error().message, HasSubstr("Parameter 'inner_width'"));
    EXPECT_THAT(r.error().message, HasSubstr("position 14"));
    EXPECT_DOUBLE_EQ(valueOf(g, "inner_width"), 764.0);
    EXPECT_EQ(g.find("inner_width")->expression, "outer_width - left_thickness - right_thickness");
}

TEST(ParameterGraph, InvalidNamesAndValuesAreRejected) {
    ParameterGraph g;
    EXPECT_EQ(g.defineConstant("1abc", 1).error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(g.defineConstant("a..b", 1).error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(g.defineConstant("", 1).error().code, ErrorCode::InvalidArgument);
    EXPECT_THAT(g.defineConstant("min", 1).error().message, HasSubstr("built-in function"));
    EXPECT_FALSE(g.defineConstant("a", std::numeric_limits<double>::infinity()).ok());
    EXPECT_FALSE(g.defineConstant("a", std::numeric_limits<double>::quiet_NaN()).ok());
    ParameterDef def = ParameterDef::constant("a", 5);
    def.lowerBound = 10;
    def.upperBound = 0;
    EXPECT_THAT(g.define(def).error().message, HasSubstr("lower bound 10 above its upper bound 0"));
    EXPECT_EQ(g.size(), 0u);
    EXPECT_EQ(g.value("a").error().code, ErrorCode::NotFound);
    EXPECT_EQ(g.find("a"), nullptr);
}

TEST(ParameterGraph, EvaluationErrorsNameTheParameterAndRollBack) {
    ParameterGraph g;
    FOS_ASSERT_OK(g.defineConstant("x", 0));
    auto r = g.defineExpression("y", "10 / x");
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("Parameter 'y' ('10 / x') cannot be evaluated: division by zero"));
    EXPECT_FALSE(g.contains("y"));

    FOS_ASSERT_OK(g.setValue("x", 2));
    FOS_ASSERT_OK(g.defineExpression("y", "10 / x"));   // 5
    FOS_ASSERT_OK(g.defineExpression("z", "sqrt(y - 1)")); // 2
    EXPECT_DOUBLE_EQ(valueOf(g, "z"), 2.0);

    r = g.setValue("x", 0);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("division by zero"));
    EXPECT_DOUBLE_EQ(valueOf(g, "x"), 2.0);
    EXPECT_DOUBLE_EQ(valueOf(g, "y"), 5.0);

    // x = 20 -> y = 0.5 -> sqrt(-0.5): fails in z after y was already recomputed; all restored.
    r = g.setValue("x", 20);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("Parameter 'z'"));
    EXPECT_THAT(r.error().message, HasSubstr("square root of a negative number"));
    EXPECT_DOUBLE_EQ(valueOf(g, "x"), 2.0);
    EXPECT_DOUBLE_EQ(valueOf(g, "y"), 5.0);
    EXPECT_DOUBLE_EQ(valueOf(g, "z"), 2.0);

    r = g.defineExpression("huge", "10 ^ (x * 200)");
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("finite"));
}

TEST(ParameterGraph, DeterministicListingAndIntrospection) {
    ParameterGraph g;
    FOS_ASSERT_OK(g.defineConstant("zeta", 1));
    FOS_ASSERT_OK(g.defineConstant("alpha", 2));
    FOS_ASSERT_OK(g.defineExpression("mid", "zeta + alpha"));
    FOS_ASSERT_OK(g.defineExpression("beta", "mid * alpha"));
    EXPECT_THAT(g.names(), ElementsAre("alpha", "beta", "mid", "zeta"));
    EXPECT_THAT(g.dependencies("mid"), ElementsAre("alpha", "zeta"));
    EXPECT_THAT(g.dependencies("alpha"), IsEmpty());
    EXPECT_THAT(g.dependencies("unknown"), IsEmpty());
    EXPECT_THAT(g.dependents("alpha"), ElementsAre("beta", "mid"));
    EXPECT_THAT(g.transitiveDependents("zeta"), ElementsAre("mid", "beta"));
    EXPECT_THAT(g.dependents("beta"), IsEmpty());

    const auto order = g.evaluationOrder();
    ASSERT_EQ(order.size(), 4u);
    const auto pos = [&](const std::string& n) { return std::find(order.begin(), order.end(), n) - order.begin(); };
    EXPECT_LT(pos("alpha"), pos("mid"));
    EXPECT_LT(pos("zeta"), pos("mid"));
    EXPECT_LT(pos("mid"), pos("beta"));
    EXPECT_EQ(order, g.evaluationOrder()); // stable

    const auto defs = g.definitions();
    ASSERT_EQ(defs.size(), 4u);
    EXPECT_EQ(defs[1].name, "beta");
    EXPECT_EQ(defs[1].expression, "mid * alpha");
    EXPECT_DOUBLE_EQ(defs[1].value, 6.0); // (1 + 2) * 2: records carry the current value
    EXPECT_FALSE(defs[0].isExpression());

    EXPECT_DOUBLE_EQ(g.evaluate("beta + 1").valueOr(0), 7.0);
    EXPECT_FALSE(g.evaluate("nope + 1").ok());
    EXPECT_FALSE(g.evaluate("1 +").ok());
}

TEST(ParameterGraph, IntegerFlagRejectsNonIntegerValues) {
    ParameterGraph g;
    ParameterDef count = ParameterDef::constant("shelf_count", 3, ParameterKind::Count);
    count.integer = true;
    FOS_ASSERT_OK(g.define(count));
    auto r = g.setValue("shelf_count", 2.5);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::ConstraintViolation);
    EXPECT_THAT(r.error().message, HasSubstr("Parameter 'shelf_count' must be a whole number but its value is 2.5"));
    EXPECT_DOUBLE_EQ(valueOf(g, "shelf_count"), 3.0);
    FOS_ASSERT_OK(g.setValue("shelf_count", 4));

    // A computed count: 1000 / 300 = 3.33 is rejected; floor() makes it valid.
    FOS_ASSERT_OK(g.defineConstant("height", 1000));
    ParameterDef rows = ParameterDef::formula("rows", "height / 300", ParameterKind::Count);
    rows.integer = true;
    r = g.define(rows);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("computed from 'height / 300'"));
    rows.expression = "floor(height / 300)";
    FOS_ASSERT_OK(g.define(rows));
    EXPECT_DOUBLE_EQ(valueOf(g, "rows"), 3.0);
    FOS_ASSERT_OK(g.setValue("height", 1200)); // exactly 4
    EXPECT_DOUBLE_EQ(valueOf(g, "rows"), 4.0);

    // Floating-point noise within 1e-9 is snapped to the whole number: 0.1 * 3 * 10 = 3.0000000000000004
    ParameterDef noisy = ParameterDef::formula("noisy", "0.1 * 3 * 10");
    noisy.integer = true;
    FOS_ASSERT_OK(g.define(noisy));
    EXPECT_EQ(valueOf(g, "noisy"), 3.0);
}

TEST(ParameterGraph, BooleanKindAcceptsOnlyZeroOrOne) {
    ParameterGraph g;
    FOS_ASSERT_OK(g.defineConstant("has_back", 1, ParameterKind::Boolean));
    EXPECT_THAT(g.setValue("has_back", 2).error().message, HasSubstr("must be 0 or 1"));
    FOS_ASSERT_OK(g.setValue("has_back", 0));
    FOS_ASSERT_OK(g.defineConstant("w", 800));
    FOS_ASSERT_OK(g.defineExpression("wide", "w > 600", ParameterKind::Boolean));
    EXPECT_DOUBLE_EQ(valueOf(g, "wide"), 1.0);
    EXPECT_STREQ(toString(ParameterKind::Boolean), "Boolean");
    EXPECT_EQ(parameterKindFromString("Length"), ParameterKind::Length);
    EXPECT_EQ(parameterKindFromString("length"), std::nullopt);
}

TEST(ParameterGraph, ObserverReceivesChangedSetAfterCommitOnly) {
    ParameterGraph g = makeCabinet();
    std::vector<ChangeSet> seen;
    const std::size_t id = g.addObserver([&](const ChangeSet& cs) { seen.push_back(cs); });

    FOS_ASSERT_OK(g.setValue("outer_width", 900));
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_THAT(seen[0].changed, ElementsAre("inner_width", "outer_width"));

    EXPECT_FALSE(g.setExpression("outer_width", "inner_width").ok()); // cycle: no notification
    EXPECT_EQ(seen.size(), 1u);

    FOS_ASSERT_OK(g.defineExpression("half", "inner_width / 2"));
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_THAT(seen[1].added, ElementsAre("half"));
    EXPECT_THAT(seen[1].changed, ElementsAre("half"));

    g.removeObserver(id);
    FOS_ASSERT_OK(g.setValue("outer_width", 1000));
    EXPECT_EQ(seen.size(), 2u);
}

TEST(ParameterGraph, RecalculateAllIsIdempotent) {
    ParameterGraph g = makeCabinet();
    FOS_ASSERT_OK(g.defineExpression("half", "inner_width / 2"));
    FOS_ASSERT_OK(g.setValue("outer_width", 1000));
    const auto before = g.definitions();
    for (int i = 0; i < 3; ++i) {
        auto r = g.recalculateAll();
        FOS_ASSERT_OK(r);
        EXPECT_TRUE(r.value().empty());
        EXPECT_THAT(g.lastRecalculation().evaluated, ElementsAre("inner_width", "half"));
        EXPECT_THAT(g.lastRecalculation().changed, IsEmpty());
    }
    const auto after = g.definitions();
    ASSERT_EQ(before.size(), after.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        EXPECT_EQ(before[i].name, after[i].name);
        EXPECT_EQ(before[i].value, after[i].value);
        EXPECT_EQ(before[i].expression, after[i].expression);
    }
    EXPECT_DOUBLE_EQ(valueOf(g, "half"), 482.0); // (1000 - 36) / 2
}
