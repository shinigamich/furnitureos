// SPDX-License-Identifier: AGPL-3.0-only
#include "test_support.hpp"

using namespace fos::parametric;
using namespace fos::parametric::test;
using fos::core::ErrorCode;
using fos::core::Severity;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

namespace {

/// cab.width 800, cab.t 18, cab.inner_width = 764, constraint 'shelf fits': inner width > 0.
ParameterGraph makeConstrainedCabinet() {
    ParameterGraph g;
    Transaction tx;
    tx.define(ParameterDef::constant("cab.width", 800, ParameterKind::Length))
        .define(ParameterDef::constant("cab.t", 18, ParameterKind::Length))
        .define(ParameterDef::formula("cab.inner_width", "cab.width - 2 * cab.t", ParameterKind::Length))
        .addConstraint({"shelf fits", "cab.inner_width > 0",
                        "Inner width must be positive (it is {cab.inner_width} mm)", ConstraintSeverity::Error});
    EXPECT_TRUE(g.apply(tx).ok());
    return g;
}

} // namespace

TEST(Transaction, StepsMayReferenceLaterDefinitions) {
    ParameterGraph g;
    Transaction tx;
    tx.define(ParameterDef::formula("inner", "width - 2 * t")) // defined before width and t
        .define(ParameterDef::constant("width", 600))
        .define(ParameterDef::constant("t", 18));
    EXPECT_EQ(tx.size(), 3u);
    auto r = g.apply(tx);
    FOS_ASSERT_OK(r);
    EXPECT_DOUBLE_EQ(valueOf(g, "inner"), 564.0); // 600 - 36
    EXPECT_THAT(r.value().added, ElementsAre("inner", "t", "width"));
    EXPECT_THAT(r.value().changed, ElementsAre("inner", "t", "width"));
}

TEST(Transaction, FailingStepRollsBackEarlierSteps) {
    ParameterGraph g = makeCabinet();
    Transaction tx;
    tx.setValue("outer_width", 1200).define(ParameterDef::constant("extra", 5)).setValue("does_not_exist", 1);
    auto r = g.apply(tx);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::NotFound);
    EXPECT_THAT(r.error().message, HasSubstr("'does_not_exist'"));
    EXPECT_DOUBLE_EQ(valueOf(g, "outer_width"), 800.0);
    EXPECT_DOUBLE_EQ(valueOf(g, "inner_width"), 764.0);
    EXPECT_FALSE(g.contains("extra"));
    ASSERT_EQ(g.lastDiagnostics().size(), 1u);
    EXPECT_EQ(g.lastDiagnostics()[0].severity, Severity::Error);
}

TEST(Transaction, EvaluationFailureRollsBackEverything) {
    ParameterGraph g = makeCabinet();
    FOS_ASSERT_OK(g.defineExpression("ratio", "1000 / inner_width"));
    // 36 - 18 - 18 = 0 -> ratio divides by zero.
    Transaction tx;
    tx.define(ParameterDef::constant("extra", 5)).setValue("outer_width", 36);
    auto r = g.apply(tx);
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("Parameter 'ratio'"));
    EXPECT_FALSE(g.contains("extra"));
    EXPECT_DOUBLE_EQ(valueOf(g, "outer_width"), 800.0);
    EXPECT_DOUBLE_EQ(valueOf(g, "inner_width"), 764.0);
    EXPECT_THAT(g.dependents("inner_width"), ElementsAre("ratio"));
}

TEST(Transaction, ConstraintFailureRollsBackAndReportsActualValues) {
    ParameterGraph g = makeConstrainedCabinet();
    EXPECT_DOUBLE_EQ(valueOf(g, "cab.inner_width"), 764.0);
    // 32 - 2 * 18 = -4
    auto r = g.apply(Transaction().define(ParameterDef::constant("other", 1)).setValue("cab.width", 32));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::ConstraintViolation);
    EXPECT_EQ(r.error().context, "shelf fits");
    EXPECT_EQ(r.error().message,
              "Constraint 'shelf fits' violated: Inner width must be positive (it is -4 mm) "
              "(cab.inner_width > 0 is false; cab.inner_width = -4).");
    EXPECT_DOUBLE_EQ(valueOf(g, "cab.width"), 800.0);
    EXPECT_DOUBLE_EQ(valueOf(g, "cab.inner_width"), 764.0);
    EXPECT_FALSE(g.contains("other"));
    ASSERT_EQ(g.lastDiagnostics().size(), 1u);
    EXPECT_EQ(g.lastDiagnostics()[0].code, "parametric.constraint.violated");
    EXPECT_EQ(g.lastDiagnostics()[0].entityId, "shelf fits");
    EXPECT_THAT(g.checkConstraints(), IsEmpty()); // the committed state is still valid
}

TEST(Transaction, AtomicChangeThatIsInvalidStepByStep) {
    ParameterGraph g;
    FOS_ASSERT_OK(g.apply(Transaction()
                              .define(ParameterDef::constant("lo", 10))
                              .define(ParameterDef::constant("hi", 20))
                              .addConstraint({"ordered", "lo <= hi", "", ConstraintSeverity::Error})));
    auto r = g.setValue("lo", 30);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().message, "Constraint 'ordered' violated: (lo <= hi is false; hi = 20, lo = 30).");
    EXPECT_DOUBLE_EQ(valueOf(g, "lo"), 10.0);
    // Both together are fine: constraints are checked once, after the last step.
    FOS_ASSERT_OK(g.apply(Transaction().setValue("lo", 30).setValue("hi", 40)));
    EXPECT_DOUBLE_EQ(valueOf(g, "lo"), 30.0);
    EXPECT_DOUBLE_EQ(valueOf(g, "hi"), 40.0);
}

TEST(Transaction, RedefineRemoveAndRenameInOneTransaction) {
    ParameterGraph g = makeCabinet();
    Transaction tx;
    tx.rename("outer_width", "w")
        .setValue("w", 1000)
        .remove("right_thickness")
        .setExpression("inner_width", "w - 2 * left_thickness");
    auto r = g.apply(tx);
    FOS_ASSERT_OK(r);
    EXPECT_DOUBLE_EQ(valueOf(g, "inner_width"), 964.0); // 1000 - 36
    EXPECT_THAT(g.names(), ElementsAre("inner_width", "left_thickness", "w"));
    EXPECT_THAT(r.value().renamed, ElementsAre(std::make_pair(std::string("outer_width"), std::string("w"))));
    EXPECT_THAT(r.value().removed, ElementsAre("right_thickness"));
    EXPECT_THAT(r.value().changed, ElementsAre("inner_width", "w")); // w: 800 -> 1000
    EXPECT_THAT(r.value().added, IsEmpty());
}

TEST(Constraints, BoundsOnConstantsAndComputedValues) {
    ParameterGraph g;
    ParameterDef count = ParameterDef::constant("shelf_count", 3, ParameterKind::Count);
    count.lowerBound = 0;
    count.upperBound = 10;
    FOS_ASSERT_OK(g.define(count));

    auto r = g.setValue("shelf_count", 11);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::ConstraintViolation);
    EXPECT_EQ(r.error().message, "Parameter 'shelf_count' = 11 is above its upper bound 10.");
    EXPECT_EQ(g.lastDiagnostics()[0].code, "parametric.parameter.out_of_bounds");
    r = g.setValue("shelf_count", -1);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().message, "Parameter 'shelf_count' = -1 is below its lower bound 0.");
    EXPECT_DOUBLE_EQ(valueOf(g, "shelf_count"), 3.0);
    FOS_ASSERT_OK(g.setValue("shelf_count", 10)); // bounds are inclusive
    FOS_ASSERT_OK(g.setValue("shelf_count", 0));

    // Bound on a computed parameter: spacing = 1964 / (count + 1) must be >= 150 mm.
    FOS_ASSERT_OK(g.setValue("shelf_count", 3));
    ParameterDef spacing = ParameterDef::formula("spacing", "1964 / (shelf_count + 1)", ParameterKind::Length);
    spacing.lowerBound = 150;
    FOS_ASSERT_OK(g.define(spacing));
    EXPECT_DOUBLE_EQ(valueOf(g, "spacing"), 491.0); // 1964 / 4
    r = g.setValue("shelf_count", 10);              // 1964 / 11 = 178.5 -> fine
    FOS_ASSERT_OK(r);
    FOS_ASSERT_OK(g.setBounds("shelf_count", 0.0, 20.0));
    r = g.setValue("shelf_count", 13); // 1964 / 14 = 140.29 < 150
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("Parameter 'spacing' = 140.2857143 is below its lower bound 150 "
                                             "(computed from '1964 / (shelf_count + 1)')."));
    EXPECT_DOUBLE_EQ(valueOf(g, "shelf_count"), 10.0);

    // Tightening bounds so that the current value violates them is rejected.
    r = g.setBounds("shelf_count", 0.0, 5.0);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(g.find("shelf_count")->upperBound, 20.0);
    FOS_ASSERT_OK(g.setBounds("shelf_count", std::nullopt, std::nullopt));
    FOS_ASSERT_OK(g.setValue("shelf_count", 12)); // 1964 / 13 = 151.08
}

TEST(Constraints, WarningDoesNotBlock) {
    ParameterGraph g = makeConstrainedCabinet();
    FOS_ASSERT_OK(g.addConstraint({"needs partition", "cab.width <= 1200",
                                   "Cabinets wider than 1200 mm need a centre partition",
                                   ConstraintSeverity::Warning}));
    auto r = g.setValue("cab.width", 1500);
    FOS_ASSERT_OK(r);
    EXPECT_DOUBLE_EQ(valueOf(g, "cab.width"), 1500.0);
    ASSERT_EQ(r.value().warnings.size(), 1u);
    EXPECT_EQ(r.value().warnings[0].severity, Severity::Warning);
    EXPECT_EQ(r.value().warnings[0].code, "parametric.constraint.violated");
    EXPECT_EQ(r.value().warnings[0].entityId, "needs partition");
    EXPECT_EQ(r.value().warnings[0].message,
              "Constraint 'needs partition' violated: Cabinets wider than 1200 mm need a centre partition "
              "(cab.width <= 1200 is false; cab.width = 1500).");
    EXPECT_FALSE(fos::core::hasErrors(g.lastDiagnostics()));
    EXPECT_EQ(g.lastDiagnostics().size(), 1u);
    EXPECT_EQ(g.checkConstraints().size(), 1u); // still standing

    // An unrelated change does not re-check (or re-report) the warning...
    FOS_ASSERT_OK(g.define(ParameterDef::constant("unrelated", 1)));
    EXPECT_EQ(g.lastRecalculation().constraintsChecked, 0u);
    // ...and fixing the cause clears it.
    r = g.setValue("cab.width", 1000);
    FOS_ASSERT_OK(r);
    EXPECT_THAT(r.value().warnings, IsEmpty());
    EXPECT_EQ(g.lastRecalculation().constraintsChecked, 2u);
    EXPECT_THAT(g.checkConstraints(), IsEmpty());
}

TEST(Constraints, DefinitionErrors) {
    ParameterGraph g = makeConstrainedCabinet();
    // Already violated when added -> rejected and not stored.
    auto r = g.addConstraint({"narrow", "cab.width < 500", "Too wide", ConstraintSeverity::Error});
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("cab.width = 800"));
    EXPECT_EQ(g.constraints().size(), 1u);

    r = g.addConstraint({"bad ref", "cab.height > 0", "", ConstraintSeverity::Error});
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("Constraint 'bad ref' refers to undefined parameter 'cab.height'"));

    r = g.addConstraint({"bad syntax", "cab.width >", "", ConstraintSeverity::Error});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::ParseError);
    EXPECT_THAT(r.error().message, HasSubstr("Constraint 'bad syntax'"));

    EXPECT_EQ(g.addConstraint({"shelf fits", "1", "", ConstraintSeverity::Error}).error().code,
              ErrorCode::AlreadyExists);
    EXPECT_EQ(g.addConstraint({"", "1", "", ConstraintSeverity::Error}).error().code, ErrorCode::InvalidArgument);

    // A constraint that cannot be evaluated blocks the change even as a warning.
    r = g.addConstraint({"ratio", "cab.width / (cab.t - 18) > 1", "", ConstraintSeverity::Warning});
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("Constraint 'ratio'"));
    EXPECT_THAT(r.error().message, HasSubstr("division by zero"));
    EXPECT_EQ(g.constraints().size(), 1u);
}

TEST(Constraints, RemoveParameterUsedByConstraintAndRemoveConstraint) {
    ParameterGraph g = makeConstrainedCabinet();
    FOS_ASSERT_OK(g.addConstraint({"min width", "cab.width >= 300", "", ConstraintSeverity::Error}));
    FOS_ASSERT_OK(g.setExpression("cab.inner_width", "700"));
    auto r = g.remove("cab.width");
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message,
                HasSubstr("Cannot remove parameter 'cab.width': it is used by constraint 'min width'."));
    EXPECT_TRUE(g.contains("cab.width"));

    FOS_ASSERT_OK(g.removeConstraint("min width"));
    EXPECT_EQ(g.removeConstraint("min width").error().code, ErrorCode::NotFound);
    FOS_ASSERT_OK(g.remove("cab.width"));
    ASSERT_EQ(g.constraints().size(), 1u);
    EXPECT_EQ(g.constraints()[0].name, "shelf fits");
}

TEST(Constraints, AllViolationsAreReportedInDiagnostics) {
    ParameterGraph g;
    FOS_ASSERT_OK(g.apply(Transaction()
                              .define(ParameterDef::constant("w", 600))
                              .addConstraint({"a min", "w >= 300", "Too narrow", ConstraintSeverity::Error})
                              .addConstraint({"b even", "w / 2 == floor(w / 2)", "Odd width", ConstraintSeverity::Error})
                              .addConstraint({"c hint", "w >= 400", "Narrow", ConstraintSeverity::Warning})));
    auto r = g.setValue("w", 255); // violates all three
    ASSERT_FALSE(r.ok());
    EXPECT_THAT(r.error().message, HasSubstr("Constraint 'a min' violated"));
    EXPECT_THAT(r.error().message, HasSubstr("1 more violation"));
    ASSERT_EQ(g.lastDiagnostics().size(), 3u);
    EXPECT_EQ(g.lastDiagnostics()[0].entityId, "a min");
    EXPECT_EQ(g.lastDiagnostics()[1].entityId, "b even");
    EXPECT_EQ(g.lastDiagnostics()[2].severity, Severity::Warning);
    EXPECT_DOUBLE_EQ(valueOf(g, "w"), 600.0);
}
