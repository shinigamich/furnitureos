// SPDX-License-Identifier: AGPL-3.0-only
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

#include "fos/parametric/expression.hpp"

namespace fos::parametric {
namespace {

int depthOf(const std::vector<ExprPtr>& args) {
    int d = 0;
    for (const auto& a : args)
        if (a) d = std::max(d, a->depth);
    return d + 1;
}

const char* symbol(BinaryOp op) {
    switch (op) {
    case BinaryOp::Add: return "+";
    case BinaryOp::Subtract: return "-";
    case BinaryOp::Multiply: return "*";
    case BinaryOp::Divide: return "/";
    case BinaryOp::Power: return "^";
    case BinaryOp::Less: return "<";
    case BinaryOp::LessEqual: return "<=";
    case BinaryOp::Greater: return ">";
    case BinaryOp::GreaterEqual: return ">=";
    case BinaryOp::Equal: return "==";
    case BinaryOp::NotEqual: return "!=";
    case BinaryOp::And: return "&&";
    case BinaryOp::Or: return "||";
    }
    return "?";
}

// Precedence levels (higher binds tighter).
constexpr int kPrecUnary = 7;
constexpr int kPrecPower = 8;
constexpr int kPrecPrimary = 9;

int precedence(BinaryOp op) {
    switch (op) {
    case BinaryOp::Or: return 1;
    case BinaryOp::And: return 2;
    case BinaryOp::Equal:
    case BinaryOp::NotEqual: return 3;
    case BinaryOp::Less:
    case BinaryOp::LessEqual:
    case BinaryOp::Greater:
    case BinaryOp::GreaterEqual: return 4;
    case BinaryOp::Add:
    case BinaryOp::Subtract: return 5;
    case BinaryOp::Multiply:
    case BinaryOp::Divide: return 6;
    case BinaryOp::Power: return kPrecPower;
    }
    return kPrecPrimary;
}

int precedence(const Expr& e) {
    switch (e.kind) {
    case Expr::Kind::Number: return (e.number < 0 || std::signbit(e.number)) ? kPrecUnary : kPrecPrimary;
    case Expr::Kind::Unary: return kPrecUnary;
    case Expr::Kind::Binary: return precedence(e.binaryOp);
    default: return kPrecPrimary;
    }
}

bool wellFormed(const Expr& e) {
    for (const auto& a : e.args)
        if (!a) return false;
    switch (e.kind) {
    case Expr::Kind::Number:
    case Expr::Kind::Identifier: return e.args.empty();
    case Expr::Kind::Unary: return e.args.size() == 1;
    case Expr::Kind::Binary: return e.args.size() == 2;
    case Expr::Kind::Call:
        switch (e.function) {
        case Function::Min:
        case Function::Max: return !e.args.empty();
        case Function::Clamp:
        case Function::If: return e.args.size() == 3;
        default: return e.args.size() == 1;
        }
    }
    return false;
}

void print(const Expr& e, std::string& out);

void printChild(const Expr& child, bool parens, std::string& out) {
    if (parens) out += '(';
    print(child, out);
    if (parens) out += ')';
}

void print(const Expr& e, std::string& out) {
    if (!wellFormed(e)) {
        out += "<malformed>";
        return;
    }
    switch (e.kind) {
    case Expr::Kind::Number: out += formatNumber(e.number); break;
    case Expr::Kind::Identifier: out += e.name; break;
    case Expr::Kind::Unary:
        out += e.unaryOp == UnaryOp::Negate ? '-' : '!';
        printChild(*e.args[0], precedence(*e.args[0]) < kPrecUnary, out);
        break;
    case Expr::Kind::Binary: {
        const int p = precedence(e.binaryOp);
        const int pl = precedence(*e.args[0]);
        const int pr = precedence(*e.args[1]);
        bool parensL = pl < p;
        bool parensR = pr <= p;
        if (e.binaryOp == BinaryOp::Power) { // right-associative; operand grammar: primary ^ unary
            parensL = pl < kPrecPrimary;
            parensR = pr < kPrecUnary;
        }
        printChild(*e.args[0], parensL, out);
        out += ' ';
        out += symbol(e.binaryOp);
        out += ' ';
        printChild(*e.args[1], parensR, out);
        break;
    }
    case Expr::Kind::Call:
        out += functionName(e.function);
        out += '(';
        for (std::size_t i = 0; i < e.args.size(); ++i) {
            if (i) out += ", ";
            print(*e.args[i], out);
        }
        out += ')';
        break;
    }
}

void collect(const Expr& e, std::vector<std::string>& out) {
    if (e.kind == Expr::Kind::Identifier) out.push_back(e.name);
    for (const auto& a : e.args)
        if (a) collect(*a, out);
}

core::Error evalError(std::string message) {
    return core::makeError(core::ErrorCode::InvalidArgument, std::move(message));
}

std::string quoted(const Expr& e) { return "'" + toString(e) + "'"; }

core::Result<double> eval(const Expr& e, const Lookup& lookup);

core::Result<double> finite(const Expr& e, double v) {
    if (std::isfinite(v)) return v;
    return evalError(quoted(e) + " does not produce a finite number (overflow or undefined operation)");
}

core::Result<double> evalBinary(const Expr& e, const Lookup& lookup) {
    auto l = eval(*e.args[0], lookup);
    if (!l) return l;
    const double a = l.value();
    if (e.binaryOp == BinaryOp::And && a == 0.0) return 0.0;
    if (e.binaryOp == BinaryOp::Or && a != 0.0) return 1.0;
    auto r = eval(*e.args[1], lookup);
    if (!r) return r;
    const double b = r.value();
    switch (e.binaryOp) {
    case BinaryOp::Add: return finite(e, a + b);
    case BinaryOp::Subtract: return finite(e, a - b);
    case BinaryOp::Multiply: return finite(e, a * b);
    case BinaryOp::Divide:
        if (b == 0.0) {
            return evalError("division by zero in " + quoted(e) + " (divisor " + quoted(*e.args[1]) + " = 0)");
        }
        return finite(e, a / b);
    case BinaryOp::Power: {
        const double v = std::pow(a, b);
        if (!std::isfinite(v)) {
            return evalError(quoted(e) + " does not produce a finite number (" + formatNumberForMessage(a) +
                             " ^ " + formatNumberForMessage(b) + ")");
        }
        return v;
    }
    case BinaryOp::Less: return a < b ? 1.0 : 0.0;
    case BinaryOp::LessEqual: return a <= b ? 1.0 : 0.0;
    case BinaryOp::Greater: return a > b ? 1.0 : 0.0;
    case BinaryOp::GreaterEqual: return a >= b ? 1.0 : 0.0;
    case BinaryOp::Equal: return a == b ? 1.0 : 0.0;
    case BinaryOp::NotEqual: return a != b ? 1.0 : 0.0;
    case BinaryOp::And:
    case BinaryOp::Or: return b != 0.0 ? 1.0 : 0.0;
    }
    return evalError("unknown operator in " + quoted(e));
}

core::Result<double> evalCall(const Expr& e, const Lookup& lookup) {
    if (e.function == Function::If) {
        auto c = eval(*e.args[0], lookup);
        if (!c) return c;
        return eval(*e.args[c.value() != 0.0 ? 1 : 2], lookup);
    }
    double v[3] = {0.0, 0.0, 0.0};
    double lo = 0.0, hi = 0.0;
    for (std::size_t i = 0; i < e.args.size(); ++i) {
        auto r = eval(*e.args[i], lookup);
        if (!r) return r;
        const double x = r.value();
        if (i < 3) v[i] = x;
        lo = i == 0 ? x : std::min(lo, x);
        hi = i == 0 ? x : std::max(hi, x);
    }
    switch (e.function) {
    case Function::Min: return lo;
    case Function::Max: return hi;
    case Function::Abs: return std::fabs(v[0]);
    case Function::Floor: return std::floor(v[0]);
    case Function::Ceil: return std::ceil(v[0]);
    case Function::Round: return std::round(v[0]);
    case Function::Sqrt:
        if (v[0] < 0.0) {
            return evalError("square root of a negative number in " + quoted(e) + " (argument = " +
                             formatNumberForMessage(v[0]) + ")");
        }
        return std::sqrt(v[0]);
    case Function::Clamp:
        if (v[1] > v[2]) {
            return evalError("clamp with lower limit " + formatNumberForMessage(v[1]) +
                             " above upper limit " + formatNumberForMessage(v[2]) + " in " + quoted(e));
        }
        return std::min(std::max(v[0], v[1]), v[2]);
    case Function::If: break;
    }
    return evalError("unknown function in " + quoted(e));
}

core::Result<double> eval(const Expr& e, const Lookup& lookup) {
    switch (e.kind) {
    case Expr::Kind::Number: return finite(e, e.number);
    case Expr::Kind::Identifier: {
        const std::optional<double> v = lookup ? lookup(e.name) : std::nullopt;
        if (!v) {
            return core::makeError(core::ErrorCode::NotFound, "undefined parameter '" + e.name + "'", e.name);
        }
        return finite(e, *v);
    }
    case Expr::Kind::Unary: {
        auto r = eval(*e.args[0], lookup);
        if (!r) return r;
        if (e.unaryOp == UnaryOp::Negate) return -r.value();
        return r.value() == 0.0 ? 1.0 : 0.0;
    }
    case Expr::Kind::Binary: return evalBinary(e, lookup);
    case Expr::Kind::Call: return evalCall(e, lookup);
    }
    return evalError("unknown node");
}

bool wellFormedDeep(const Expr& e, int budget) {
    if (budget <= 0 || !wellFormed(e)) return false;
    for (const auto& a : e.args)
        if (!wellFormedDeep(*a, budget - 1)) return false;
    return true;
}

} // namespace

ExprPtr makeNumber(double value) {
    auto e = std::make_shared<Expr>();
    e->kind = Expr::Kind::Number;
    e->number = value;
    return e;
}

ExprPtr makeIdentifier(std::string name) {
    auto e = std::make_shared<Expr>();
    e->kind = Expr::Kind::Identifier;
    e->name = std::move(name);
    return e;
}

ExprPtr makeUnary(UnaryOp op, ExprPtr operand) {
    auto e = std::make_shared<Expr>();
    e->kind = Expr::Kind::Unary;
    e->unaryOp = op;
    e->args.push_back(std::move(operand));
    e->depth = depthOf(e->args);
    return e;
}

ExprPtr makeBinary(BinaryOp op, ExprPtr lhs, ExprPtr rhs) {
    auto e = std::make_shared<Expr>();
    e->kind = Expr::Kind::Binary;
    e->binaryOp = op;
    e->args.push_back(std::move(lhs));
    e->args.push_back(std::move(rhs));
    e->depth = depthOf(e->args);
    return e;
}

ExprPtr makeCall(Function function, std::vector<ExprPtr> args) {
    auto e = std::make_shared<Expr>();
    e->kind = Expr::Kind::Call;
    e->function = function;
    e->args = std::move(args);
    e->depth = depthOf(e->args);
    return e;
}

core::Result<double> evaluate(const Expr& expr, const Lookup& lookup) {
    if (!wellFormedDeep(expr, kMaxExpressionDepth)) {
        return core::makeError(core::ErrorCode::Internal,
                               "malformed expression tree (wrong operand count, null operand or depth above " +
                                   std::to_string(kMaxExpressionDepth) + ")");
    }
    return eval(expr, lookup);
}

std::vector<std::string> referencedIdentifiers(const Expr& expr) {
    std::vector<std::string> names;
    collect(expr, names);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

std::string toString(const Expr& expr) {
    std::string out;
    print(expr, out);
    return out;
}

ExprPtr renameIdentifiers(const ExprPtr& expr, const std::function<std::string(const std::string&)>& rename) {
    if (!expr) return expr;
    if (expr->kind == Expr::Kind::Identifier) return makeIdentifier(rename(expr->name));
    if (expr->args.empty()) return expr;
    auto copy = std::make_shared<Expr>(*expr);
    for (auto& a : copy->args) a = renameIdentifiers(a, rename);
    return copy;
}

bool structurallyEqual(const Expr& a, const Expr& b) {
    if (a.kind != b.kind || a.args.size() != b.args.size()) return false;
    switch (a.kind) {
    case Expr::Kind::Number:
        if (a.number != b.number) return false;
        break;
    case Expr::Kind::Identifier:
        if (a.name != b.name) return false;
        break;
    case Expr::Kind::Unary:
        if (a.unaryOp != b.unaryOp) return false;
        break;
    case Expr::Kind::Binary:
        if (a.binaryOp != b.binaryOp) return false;
        break;
    case Expr::Kind::Call:
        if (a.function != b.function) return false;
        break;
    }
    for (std::size_t i = 0; i < a.args.size(); ++i) {
        if (!a.args[i] || !b.args[i]) {
            if (a.args[i] != b.args[i]) return false;
            continue;
        }
        if (!structurallyEqual(*a.args[i], *b.args[i])) return false;
    }
    return true;
}

std::string formatNumber(double value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value < 0 ? "-inf" : "inf";
    char buf[40];
    const auto res = std::to_chars(buf, buf + sizeof(buf), value);
    std::string s(buf, res.ptr);
    // Keep the text inside the expression grammar: "1e+22" is accepted by the lexer as written.
    return s;
}

std::string formatNumberForMessage(double value) {
    if (!std::isfinite(value)) return formatNumber(value);
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.10g", value);
    return buf == std::string("-0") ? "0" : std::string(buf);
}

} // namespace fos::parametric
