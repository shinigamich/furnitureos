// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Expression language of the parametric engine: AST, parser, evaluator, printer.
/// All values are doubles (millimetres, degrees or plain numbers; booleans are 0 / 1).
/// The grammar is documented in src/parametric/README.md.
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fos/core/result.hpp"

namespace fos::parametric {

enum class UnaryOp { Negate, Not };
enum class BinaryOp {
    Add, Subtract, Multiply, Divide, Power,
    Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual,
    And, Or,
};
enum class Function { Min, Max, Abs, Floor, Ceil, Round, Sqrt, Clamp, If };

struct Expr;
/// Expressions are immutable and shared; copying an ExprPtr is cheap.
using ExprPtr = std::shared_ptr<const Expr>;

/// AST node. Build nodes with the make* factories (they maintain `depth`).
struct Expr {
    enum class Kind { Number, Identifier, Unary, Binary, Call };
    Kind kind = Kind::Number;
    double number = 0.0;                 ///< Kind::Number
    std::string name;                    ///< Kind::Identifier: parameter name, e.g. "cab.inner_width"
    UnaryOp unaryOp = UnaryOp::Negate;   ///< Kind::Unary (args.size() == 1)
    BinaryOp binaryOp = BinaryOp::Add;   ///< Kind::Binary (args.size() == 2)
    Function function = Function::Min;   ///< Kind::Call
    std::vector<ExprPtr> args;
    int depth = 1;                       ///< Height of the subtree (a leaf has depth 1).
};

/// Maximum AST depth accepted by the parser and the evaluator (protects the call stack).
inline constexpr int kMaxExpressionDepth = 256;
/// Maximum length in characters of an expression text accepted by the parser.
inline constexpr std::size_t kMaxExpressionLength = 16384;

ExprPtr makeNumber(double value);
ExprPtr makeIdentifier(std::string name);
ExprPtr makeUnary(UnaryOp op, ExprPtr operand);
ExprPtr makeBinary(BinaryOp op, ExprPtr lhs, ExprPtr rhs);
ExprPtr makeCall(Function function, std::vector<ExprPtr> args);

/// Machine-readable details of a parse error (for highlighting in an editor).
struct ParseFailure {
    std::size_t position = 0; ///< 0-based character offset into the expression text.
    std::string expected;     ///< What the parser expected, e.g. "')' or an operator".
    std::string found;        ///< What it found, e.g. "'*'" or "end of expression".
};

/// Parses `text`. On failure returns ErrorCode::ParseError whose message contains the 0-based
/// position, what was expected and what was found; `failure` (if given) receives the same details.
core::Result<ExprPtr> parseExpression(std::string_view text, ParseFailure* failure = nullptr);

/// Resolves a parameter name to its value; std::nullopt when the name is unknown.
using Lookup = std::function<std::optional<double>(const std::string&)>;

/// Evaluates an expression. Errors (never exceptions): unknown identifier (NotFound), division by
/// zero, sqrt of a negative number, clamp with lo > hi, any non-finite intermediate or final result
/// (InvalidArgument). Messages quote the offending sub-expression and operand values.
/// `&&`, `||` short-circuit and `if(c, a, b)` evaluates only the selected branch.
/// Comparisons are exact (no tolerance) and yield 1 or 0; any non-zero value counts as true.
core::Result<double> evaluate(const Expr& expr, const Lookup& lookup);

/// Names of all identifiers referenced by the expression, sorted and unique.
std::vector<std::string> referencedIdentifiers(const Expr& expr);

/// Canonical text: single spaces around binary operators, ", " between arguments, minimal
/// parentheses, shortest round-trip number format. For every AST produced by parseExpression,
/// `parseExpression(toString(e))` is structurally equal to `e`.
std::string toString(const Expr& expr);

/// Returns a copy of the expression with every identifier replaced by `rename(identifier)`.
ExprPtr renameIdentifiers(const ExprPtr& expr, const std::function<std::string(const std::string&)>& rename);

bool structurallyEqual(const Expr& a, const Expr& b);

/// True for `segment ("." segment)*` where segment = [A-Za-z_][A-Za-z0-9_]*.
bool isValidIdentifier(std::string_view name);
/// True for the built-in function names (min, max, abs, floor, ceil, round, sqrt, clamp, if).
bool isFunctionName(std::string_view name);
const char* functionName(Function function) noexcept;

/// Shortest decimal text that parses back to exactly `value` (e.g. 764 -> "764", 0.1 -> "0.1").
std::string formatNumber(double value);
/// Compact text for messages: at most 10 significant digits (0.1 + 0.2 -> "0.3").
std::string formatNumberForMessage(double value);

} // namespace fos::parametric
