// SPDX-License-Identifier: AGPL-3.0-only
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <map>

#include "fos/parametric/expression.hpp"

using namespace fos::parametric;
using fos::core::ErrorCode;
using ::testing::ElementsAre;
using ::testing::HasSubstr;

namespace {

using Vars = std::map<std::string, double>;

Lookup lookupOf(const Vars& vars) {
    return [vars](const std::string& name) -> std::optional<double> {
        const auto it = vars.find(name);
        if (it == vars.end()) return std::nullopt;
        return it->second;
    };
}

/// Parses and evaluates; fails the test on any error.
double calc(const std::string& text, const Vars& vars = {}) {
    auto parsed = parseExpression(text);
    EXPECT_TRUE(parsed.ok()) << text << ": " << (parsed.ok() ? "" : parsed.error().message);
    if (!parsed) return -12345.0;
    auto value = evaluate(*parsed.value(), lookupOf(vars));
    EXPECT_TRUE(value.ok()) << text << ": " << (value.ok() ? "" : value.error().message);
    return value.valueOr(-12345.0);
}

std::string evalError(const std::string& text, const Vars& vars = {}) {
    auto parsed = parseExpression(text);
    EXPECT_TRUE(parsed.ok()) << text;
    if (!parsed) return {};
    auto value = evaluate(*parsed.value(), lookupOf(vars));
    EXPECT_FALSE(value.ok()) << text;
    return value.ok() ? std::string() : value.error().message;
}

ParseFailure parseFailure(const std::string& text) {
    ParseFailure failure;
    auto parsed = parseExpression(text, &failure);
    EXPECT_FALSE(parsed.ok()) << text;
    if (!parsed) {
        EXPECT_EQ(parsed.error().code, ErrorCode::ParseError);
        EXPECT_THAT(parsed.error().message, HasSubstr("position " + std::to_string(failure.position)));
        EXPECT_THAT(parsed.error().message, HasSubstr(failure.expected));
        EXPECT_THAT(parsed.error().message, HasSubstr(failure.found));
    }
    return failure;
}

std::string canonical(const std::string& text) {
    auto parsed = parseExpression(text);
    EXPECT_TRUE(parsed.ok()) << text;
    return parsed ? toString(*parsed.value()) : std::string();
}

} // namespace

TEST(Expression, NumericLiterals) {
    EXPECT_DOUBLE_EQ(calc("42"), 42.0);
    EXPECT_DOUBLE_EQ(calc("18.5"), 18.5);
    EXPECT_DOUBLE_EQ(calc(".5"), 0.5);
    EXPECT_DOUBLE_EQ(calc("1.5e2"), 150.0);
    EXPECT_DOUBLE_EQ(calc("1e-3"), 0.001);
    EXPECT_DOUBLE_EQ(calc("  7  "), 7.0);
}

TEST(Expression, ArithmeticPrecedenceAndAssociativity) {
    EXPECT_DOUBLE_EQ(calc("1 + 2 * 3"), 7.0);      // 1 + 6
    EXPECT_DOUBLE_EQ(calc("(1 + 2) * 3"), 9.0);    // 3 * 3
    EXPECT_DOUBLE_EQ(calc("10 - 4 - 3"), 3.0);     // (10 - 4) - 3, left-associative
    EXPECT_DOUBLE_EQ(calc("100 / 10 / 5"), 2.0);   // (100 / 10) / 5
    EXPECT_DOUBLE_EQ(calc("2 ^ 3 ^ 2"), 512.0);    // 2 ^ (3 ^ 2) = 2 ^ 9, right-associative
    EXPECT_DOUBLE_EQ(calc("(2 ^ 3) ^ 2"), 64.0);   // 8 ^ 2
    EXPECT_DOUBLE_EQ(calc("-2 ^ 2"), -4.0);        // -(2 ^ 2): power binds tighter than unary minus
    EXPECT_DOUBLE_EQ(calc("(-2) ^ 2"), 4.0);
    EXPECT_DOUBLE_EQ(calc("2 ^ -1"), 0.5);
    EXPECT_DOUBLE_EQ(calc("2 * 3 ^ 2"), 18.0);     // 2 * 9
    EXPECT_DOUBLE_EQ(calc("--5"), 5.0);
    EXPECT_DOUBLE_EQ(calc("3 - -2"), 5.0);
    EXPECT_DOUBLE_EQ(calc("-3 * -2"), 6.0);
    EXPECT_DOUBLE_EQ(calc("7 - 2 * 3 + 1"), 2.0);  // 7 - 6 + 1
}

TEST(Expression, ComparisonsAndLogicYieldZeroOrOne) {
    EXPECT_DOUBLE_EQ(calc("1 < 2"), 1.0);
    EXPECT_DOUBLE_EQ(calc("2 < 2"), 0.0);
    EXPECT_DOUBLE_EQ(calc("2 <= 2"), 1.0);
    EXPECT_DOUBLE_EQ(calc("3 > 2"), 1.0);
    EXPECT_DOUBLE_EQ(calc("2 >= 3"), 0.0);
    EXPECT_DOUBLE_EQ(calc("2 == 2"), 1.0);
    EXPECT_DOUBLE_EQ(calc("2 != 2"), 0.0);
    EXPECT_DOUBLE_EQ(calc("1 + 2 < 4 && 3 >= 3"), 1.0); // (3 < 4) && (3 >= 3)
    EXPECT_DOUBLE_EQ(calc("1 < 2 == 1"), 1.0);          // (1 < 2) == 1
    EXPECT_DOUBLE_EQ(calc("0 || 0 && 1"), 0.0);         // 0 || (0 && 1)
    EXPECT_DOUBLE_EQ(calc("1 || 0 && 0"), 1.0);         // && binds tighter than ||
    EXPECT_DOUBLE_EQ(calc("!0"), 1.0);
    EXPECT_DOUBLE_EQ(calc("!5"), 0.0);
    EXPECT_DOUBLE_EQ(calc("!0 + 1"), 2.0);              // (!0) + 1
    EXPECT_DOUBLE_EQ(calc("!(1 == 2)"), 1.0);
    EXPECT_DOUBLE_EQ(calc("5 && 7"), 1.0);              // non-zero is true, result normalised to 1
}

TEST(Expression, Functions) {
    EXPECT_DOUBLE_EQ(calc("min(3, 1, 2)"), 1.0);
    EXPECT_DOUBLE_EQ(calc("max(3, 1, 2)"), 3.0);
    EXPECT_DOUBLE_EQ(calc("max(4)"), 4.0);
    EXPECT_DOUBLE_EQ(calc("abs(-3.5)"), 3.5);
    EXPECT_DOUBLE_EQ(calc("floor(2.7)"), 2.0);
    EXPECT_DOUBLE_EQ(calc("floor(-2.1)"), -3.0);
    EXPECT_DOUBLE_EQ(calc("ceil(2.1)"), 3.0);
    EXPECT_DOUBLE_EQ(calc("round(2.5)"), 3.0);   // half away from zero
    EXPECT_DOUBLE_EQ(calc("round(-2.5)"), -3.0);
    EXPECT_DOUBLE_EQ(calc("round(2.4)"), 2.0);
    EXPECT_DOUBLE_EQ(calc("sqrt(16)"), 4.0);
    EXPECT_DOUBLE_EQ(calc("clamp(15, 0, 10)"), 10.0);
    EXPECT_DOUBLE_EQ(calc("clamp(-5, 0, 10)"), 0.0);
    EXPECT_DOUBLE_EQ(calc("clamp(5, 0, 10)"), 5.0);
    EXPECT_DOUBLE_EQ(calc("if(1 > 2, 10, 20)"), 20.0);
    EXPECT_DOUBLE_EQ(calc("if(2 > 1, 10, 20)"), 10.0);
    // Shelf count from a height: floor((2000 - 2 * 18) / 320) = floor(1964 / 320) = floor(6.1375) = 6
    EXPECT_DOUBLE_EQ(calc("floor((h - 2 * t) / pitch)", {{"h", 2000}, {"t", 18}, {"pitch", 320}}), 6.0);
}

TEST(Expression, IdentifiersWithDotsAndUnderscores) {
    const Vars vars{{"cab.inner_width", 764}, {"shelf_count", 3}, {"_g.t2", 2}};
    EXPECT_DOUBLE_EQ(calc("cab.inner_width + shelf_count * _g.t2", vars), 770.0); // 764 + 6
    auto parsed = parseExpression("b.x + a * b.x - min(c_1, a)");
    ASSERT_TRUE(parsed.ok());
    EXPECT_THAT(referencedIdentifiers(*parsed.value()), ElementsAre("a", "b.x", "c_1"));
}

TEST(Expression, LazyBranchesAndShortCircuit) {
    const Vars zero{{"x", 0}};
    EXPECT_DOUBLE_EQ(calc("if(x == 0, 0, 100 / x)", zero), 0.0);  // 100 / 0 is never evaluated
    EXPECT_DOUBLE_EQ(calc("x != 0 && 100 / x > 1", zero), 0.0);
    EXPECT_DOUBLE_EQ(calc("x == 0 || 100 / x > 1", zero), 1.0);
    EXPECT_DOUBLE_EQ(calc("if(x == 0, 0, 100 / x)", {{"x", 4}}), 25.0);
    // The untaken branch may even reference an unknown parameter.
    EXPECT_DOUBLE_EQ(calc("if(1, 5, missing)"), 5.0);
}

TEST(Expression, ParseErrorsReportPositionAndExpectation) {
    ParseFailure f = parseFailure("1 +");
    EXPECT_EQ(f.position, 3u);
    EXPECT_EQ(f.found, "end of expression");
    EXPECT_THAT(f.expected, HasSubstr("a number"));

    f = parseFailure("a + * b");
    EXPECT_EQ(f.position, 4u);
    EXPECT_EQ(f.found, "'*'");

    f = parseFailure("(1 + 2");
    EXPECT_EQ(f.position, 6u);
    EXPECT_THAT(f.expected, HasSubstr("')'"));

    f = parseFailure("foo(1)");
    EXPECT_EQ(f.position, 0u);
    EXPECT_THAT(f.expected, HasSubstr("known function"));
    EXPECT_EQ(f.found, "'foo'");

    f = parseFailure("clamp(1, 2)");
    EXPECT_THAT(f.expected, HasSubstr("exactly 3 arguments (2 given)"));

    f = parseFailure("min()");
    EXPECT_EQ(f.position, 4u);

    f = parseFailure("1 $ 2");
    EXPECT_EQ(f.position, 2u);
    EXPECT_EQ(f.found, "'$'");

    f = parseFailure("a.");
    EXPECT_EQ(f.position, 2u);

    f = parseFailure("a = b"); // assignment / single '=' is not an operator
    EXPECT_EQ(f.position, 2u);
    EXPECT_EQ(f.found, "'='");

    f = parseFailure("a b");
    EXPECT_EQ(f.position, 2u);
    EXPECT_THAT(f.expected, HasSubstr("operator"));

    f = parseFailure("");
    EXPECT_EQ(f.position, 0u);

    parseFailure("1.2.3");
    parseFailure("2x");
    parseFailure("1e999"); // out of double range
    parseFailure("a & b");
    parseFailure("min(1,)");
    parseFailure(")");

    auto r = parseExpression("a + * b");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().message,
              "Parse error at position 4 in 'a + * b': expected a number, a parameter name, a function call, "
              "'(', '-' or '!' but found '*'.");
}

TEST(Expression, HostileInputDoesNotCrash) {
    parseFailure(std::string(100000, '(')); // too long
    parseFailure(std::string(5000, '('));   // too deep
    parseFailure(std::string(5000, '-') + "1");
    std::string sum = "1";
    for (int i = 1; i < 200; ++i) sum += " + 1";
    EXPECT_DOUBLE_EQ(calc(sum), 200.0); // 200 terms: depth 200 <= 256
    for (int i = 0; i < 200; ++i) sum += " + 1";
    ParseFailure f = parseFailure(sum); // 400 terms: left-deep tree deeper than 256
    EXPECT_THAT(f.expected, HasSubstr("nesting depth"));
}

TEST(Expression, EvaluationErrors) {
    EXPECT_THAT(evalError("10 / (a - a)", {{"a", 3}}), HasSubstr("division by zero in '10 / (a - a)'"));
    EXPECT_THAT(evalError("sqrt(a - 20)", {{"a", 4}}), HasSubstr("square root of a negative number"));
    EXPECT_THAT(evalError("sqrt(a - 20)", {{"a", 4}}), HasSubstr("-16"));
    EXPECT_THAT(evalError("10 ^ 400"), HasSubstr("finite"));
    EXPECT_THAT(evalError("1e308 * 10"), HasSubstr("finite"));
    EXPECT_THAT(evalError("(0 - 8) ^ 0.5"), HasSubstr("finite")); // NaN
    EXPECT_THAT(evalError("clamp(1, 5, 2)"), HasSubstr("clamp"));
    EXPECT_THAT(evalError("a + missing.name", {{"a", 1}}), HasSubstr("undefined parameter 'missing.name'"));

    auto parsed = parseExpression("x + 1");
    ASSERT_TRUE(parsed.ok());
    auto r = evaluate(*parsed.value(), Lookup{});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::NotFound);
}

TEST(Expression, MalformedHandBuiltTreeIsAnErrorNotACrash) {
    Expr bad;
    bad.kind = Expr::Kind::Binary; // no operands
    EXPECT_FALSE(evaluate(bad, Lookup{}).ok());
    EXPECT_EQ(toString(bad), "<malformed>");
    auto withNull = makeUnary(UnaryOp::Negate, nullptr);
    EXPECT_FALSE(evaluate(*withNull, Lookup{}).ok());
}

TEST(Expression, CanonicalToString) {
    EXPECT_EQ(canonical("1+2*3"), "1 + 2 * 3");
    EXPECT_EQ(canonical("(1+2)*3"), "(1 + 2) * 3");
    EXPECT_EQ(canonical("a-(b-c)"), "a - (b - c)");
    EXPECT_EQ(canonical("(a-b)-c"), "a - b - c");
    EXPECT_EQ(canonical("a/(b*c)"), "a / (b * c)");
    EXPECT_EQ(canonical("(a^b)^c"), "(a ^ b) ^ c");
    EXPECT_EQ(canonical("a^(b^c)"), "a ^ b ^ c");
    EXPECT_EQ(canonical("-(a+b)"), "-(a + b)");
    EXPECT_EQ(canonical("(-a)^2"), "(-a) ^ 2");
    EXPECT_EQ(canonical("-a^2"), "-a ^ 2");
    EXPECT_EQ(canonical("a^-b"), "a ^ -b");
    EXPECT_EQ(canonical("a - -b"), "a - -b");
    EXPECT_EQ(canonical("!(a<b)"), "!(a < b)");
    EXPECT_EQ(canonical("!a<b"), "!a < b");
    EXPECT_EQ(canonical("(a||b)&&c"), "(a || b) && c");
    EXPECT_EQ(canonical("a||b&&c"), "a || b && c");
    EXPECT_EQ(canonical("min( a,2 ,cab.w )"), "min(a, 2, cab.w)");
    EXPECT_EQ(canonical("if(a>0,a,0-a)"), "if(a > 0, a, 0 - a)");
    EXPECT_EQ(canonical("outer_width-left_thickness-right_thickness"),
              "outer_width - left_thickness - right_thickness");
    EXPECT_EQ(canonical("0.50 + 1e3 + 18.0"), "0.5 + 1000 + 18");
}

TEST(Expression, ToStringRoundTrips) {
    const char* cases[] = {
        "1 + 2 * 3", "(1 + 2) * 3", "a - (b - c)", "a - b - c", "(a ^ b) ^ c", "a ^ b ^ c", "-(a + b)",
        "(-a) ^ 2", "-a ^ 2", "a ^ -b ^ c", "!(a < b) || c >= d && !e", "a - -b", "--a", "!!a", "-!a",
        "min(a, max(b, 2), clamp(c, 0, 10))", "if(a == b, sqrt(abs(c)), floor(d) + ceil(e) - round(f))",
        "(a < b) < c", "a < (b < c)", "(a == b) != (c == d)", "a / (b / c)", "a * (b + c) / d",
        "0.1 + 1e-7 * 3.25e10", "x.y.z_1 * _q", "(a && b) || (c && d)", "a && (b || c)", "2 ^ (a + 1)",
        "(a + 1) ^ 2", "-(a ^ 2)", "(a * b) ^ (c / d)", "1e300 * 1e-300",
    };
    for (const char* text : cases) {
        auto first = parseExpression(text);
        ASSERT_TRUE(first.ok()) << text;
        const std::string printed = toString(*first.value());
        auto second = parseExpression(printed);
        ASSERT_TRUE(second.ok()) << printed;
        EXPECT_TRUE(structurallyEqual(*first.value(), *second.value())) << text << " -> " << printed;
        EXPECT_EQ(toString(*second.value()), printed) << text;
    }
}

TEST(Expression, HandBuiltNegativeLiteralsPrintSafely) {
    // (-2) ^ 2 built with a negative literal must not print as "-2 ^ 2" (= -4).
    const ExprPtr e = makeBinary(BinaryOp::Power, makeNumber(-2), makeNumber(2));
    EXPECT_EQ(toString(*e), "(-2) ^ 2");
    auto reparsed = parseExpression(toString(*e));
    ASSERT_TRUE(reparsed.ok());
    EXPECT_DOUBLE_EQ(evaluate(*reparsed.value(), Lookup{}).valueOr(0), 4.0);
}

TEST(Expression, RenameIdentifiers) {
    auto parsed = parseExpression("width - 2 * t + min(width, depth)");
    ASSERT_TRUE(parsed.ok());
    const ExprPtr renamed = renameIdentifiers(
        parsed.value(), [](const std::string& id) { return id == "width" ? std::string("cab.w") : id; });
    EXPECT_EQ(toString(*renamed), "cab.w - 2 * t + min(cab.w, depth)");
    EXPECT_EQ(toString(*parsed.value()), "width - 2 * t + min(width, depth)"); // original untouched
}

TEST(Expression, IdentifierValidationAndNumberFormat) {
    EXPECT_TRUE(isValidIdentifier("width"));
    EXPECT_TRUE(isValidIdentifier("cab1.inner_width"));
    EXPECT_TRUE(isValidIdentifier("_a._b2"));
    EXPECT_FALSE(isValidIdentifier(""));
    EXPECT_FALSE(isValidIdentifier("1abc"));
    EXPECT_FALSE(isValidIdentifier("a..b"));
    EXPECT_FALSE(isValidIdentifier("a."));
    EXPECT_FALSE(isValidIdentifier(".a"));
    EXPECT_FALSE(isValidIdentifier("a.1"));
    EXPECT_FALSE(isValidIdentifier("a b"));
    EXPECT_FALSE(isValidIdentifier("a-b"));
    EXPECT_TRUE(isFunctionName("clamp"));
    EXPECT_FALSE(isFunctionName("width"));
    EXPECT_EQ(formatNumber(764), "764");
    EXPECT_EQ(formatNumber(0.1), "0.1");
    EXPECT_EQ(formatNumber(-18.5), "-18.5");
    EXPECT_EQ(formatNumberForMessage(0.1 + 0.2), "0.3");
    EXPECT_EQ(formatNumberForMessage(-4), "-4");
}
