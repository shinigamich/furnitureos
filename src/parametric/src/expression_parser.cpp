// SPDX-License-Identifier: AGPL-3.0-only
#include <algorithm>
#include <charconv>

#include "fos/parametric/expression.hpp"

namespace fos::parametric {
namespace {

bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

struct FunctionInfo {
    const char* name;
    Function function;
    int minArgs;
    int maxArgs; ///< -1 = unlimited
};
constexpr FunctionInfo kFunctions[] = {
    {"min", Function::Min, 1, -1},    {"max", Function::Max, 1, -1},   {"abs", Function::Abs, 1, 1},
    {"floor", Function::Floor, 1, 1}, {"ceil", Function::Ceil, 1, 1},  {"round", Function::Round, 1, 1},
    {"sqrt", Function::Sqrt, 1, 1},   {"clamp", Function::Clamp, 3, 3}, {"if", Function::If, 3, 3},
};

const FunctionInfo* findFunction(std::string_view name) {
    for (const auto& f : kFunctions)
        if (name == f.name) return &f;
    return nullptr;
}

class Parser {
public:
    explicit Parser(std::string_view text) : s_(text) {}

    ExprPtr run() {
        if (s_.size() > kMaxExpressionLength) {
            return fail(kMaxExpressionLength,
                        "an expression of at most " + std::to_string(kMaxExpressionLength) + " characters");
        }
        ExprPtr e = parseOr();
        if (!e) return nullptr;
        skipWs();
        if (pos_ != s_.size()) return fail(pos_, "an operator or the end of the expression");
        return e;
    }

    const ParseFailure& failure() const { return failure_; }

private:
    void skipWs() {
        while (pos_ < s_.size() && isSpace(s_[pos_])) ++pos_;
    }
    char peek(std::size_t offset = 0) const { return pos_ + offset < s_.size() ? s_[pos_ + offset] : '\0'; }

    std::string describeAt(std::size_t pos) const {
        if (pos >= s_.size()) return "end of expression";
        std::size_t end = pos;
        const char c = s_[pos];
        if (isAlpha(c) || isDigit(c) || c == '.') {
            while (end < s_.size() && (isAlpha(s_[end]) || isDigit(s_[end]) || s_[end] == '.')) ++end;
        } else {
            end = pos + 1;
            if (end < s_.size()) {
                const std::string_view two = s_.substr(pos, 2);
                if (two == "<=" || two == ">=" || two == "==" || two == "!=" || two == "&&" || two == "||") ++end;
            }
        }
        end = std::min(end, pos + 24);
        return "'" + std::string(s_.substr(pos, end - pos)) + "'";
    }

    ExprPtr fail(std::size_t pos, std::string expected) {
        if (!failed_) {
            failed_ = true;
            failure_.position = pos;
            failure_.expected = std::move(expected);
            failure_.found = describeAt(pos);
        }
        return nullptr;
    }

    /// Wraps node creation with the depth limit.
    ExprPtr checked(ExprPtr e, std::size_t pos) {
        if (e && e->depth > kMaxExpressionDepth) {
            return fail(pos, "a simpler expression (nesting depth is limited to " +
                                 std::to_string(kMaxExpressionDepth) + ")");
        }
        return e;
    }

    bool eat(std::string_view op) {
        skipWs();
        if (s_.substr(pos_, op.size()) != op) return false;
        pos_ += op.size();
        return true;
    }

    ExprPtr parseOr() {
        ExprPtr lhs = parseAnd();
        while (lhs) {
            const std::size_t at = (skipWs(), pos_);
            if (!eat("||")) break;
            ExprPtr rhs = parseAnd();
            if (!rhs) return nullptr;
            lhs = checked(makeBinary(BinaryOp::Or, std::move(lhs), std::move(rhs)), at);
        }
        return lhs;
    }

    ExprPtr parseAnd() {
        ExprPtr lhs = parseEquality();
        while (lhs) {
            const std::size_t at = (skipWs(), pos_);
            if (!eat("&&")) break;
            ExprPtr rhs = parseEquality();
            if (!rhs) return nullptr;
            lhs = checked(makeBinary(BinaryOp::And, std::move(lhs), std::move(rhs)), at);
        }
        return lhs;
    }

    ExprPtr parseEquality() {
        ExprPtr lhs = parseRelational();
        while (lhs) {
            const std::size_t at = (skipWs(), pos_);
            BinaryOp op;
            if (eat("==")) op = BinaryOp::Equal;
            else if (eat("!=")) op = BinaryOp::NotEqual;
            else break;
            ExprPtr rhs = parseRelational();
            if (!rhs) return nullptr;
            lhs = checked(makeBinary(op, std::move(lhs), std::move(rhs)), at);
        }
        return lhs;
    }

    ExprPtr parseRelational() {
        ExprPtr lhs = parseAdditive();
        while (lhs) {
            const std::size_t at = (skipWs(), pos_);
            BinaryOp op;
            if (eat("<=")) op = BinaryOp::LessEqual;
            else if (eat(">=")) op = BinaryOp::GreaterEqual;
            else if (eat("<")) op = BinaryOp::Less;
            else if (eat(">")) op = BinaryOp::Greater;
            else break;
            ExprPtr rhs = parseAdditive();
            if (!rhs) return nullptr;
            lhs = checked(makeBinary(op, std::move(lhs), std::move(rhs)), at);
        }
        return lhs;
    }

    ExprPtr parseAdditive() {
        ExprPtr lhs = parseMultiplicative();
        while (lhs) {
            const std::size_t at = (skipWs(), pos_);
            BinaryOp op;
            if (eat("+")) op = BinaryOp::Add;
            else if (eat("-")) op = BinaryOp::Subtract;
            else break;
            ExprPtr rhs = parseMultiplicative();
            if (!rhs) return nullptr;
            lhs = checked(makeBinary(op, std::move(lhs), std::move(rhs)), at);
        }
        return lhs;
    }

    ExprPtr parseMultiplicative() {
        ExprPtr lhs = parseUnary();
        while (lhs) {
            const std::size_t at = (skipWs(), pos_);
            BinaryOp op;
            if (eat("*")) op = BinaryOp::Multiply;
            else if (eat("/")) op = BinaryOp::Divide;
            else break;
            ExprPtr rhs = parseUnary();
            if (!rhs) return nullptr;
            lhs = checked(makeBinary(op, std::move(lhs), std::move(rhs)), at);
        }
        return lhs;
    }

    ExprPtr parseUnary() {
        skipWs();
        const std::size_t at = pos_;
        UnaryOp op;
        if (peek() == '-') op = UnaryOp::Negate;
        else if (peek() == '!' && peek(1) != '=') op = UnaryOp::Not;
        else return parsePower();
        ++pos_;
        if (++recursion_ > kMaxExpressionDepth) {
            return fail(at, "a simpler expression (nesting depth is limited to " +
                                std::to_string(kMaxExpressionDepth) + ")");
        }
        ExprPtr operand = parseUnary();
        --recursion_;
        if (!operand) return nullptr;
        return checked(makeUnary(op, std::move(operand)), at);
    }

    ExprPtr parsePower() {
        ExprPtr base = parsePrimary();
        if (!base) return nullptr;
        const std::size_t at = (skipWs(), pos_);
        if (!eat("^")) return base;
        if (++recursion_ > kMaxExpressionDepth) {
            return fail(at, "a simpler expression (nesting depth is limited to " +
                                std::to_string(kMaxExpressionDepth) + ")");
        }
        ExprPtr exponent = parseUnary(); // right-associative; allows 2 ^ -3
        --recursion_;
        if (!exponent) return nullptr;
        return checked(makeBinary(BinaryOp::Power, std::move(base), std::move(exponent)), at);
    }

    ExprPtr parseNumber() {
        const std::size_t start = pos_;
        std::size_t p = pos_;
        while (p < s_.size() && isDigit(s_[p])) ++p;
        if (p < s_.size() && s_[p] == '.') {
            ++p;
            while (p < s_.size() && isDigit(s_[p])) ++p;
        }
        if (p == start + 1 && s_[start] == '.') return fail(start, "a digit after '.'");
        if (p < s_.size() && (s_[p] == 'e' || s_[p] == 'E')) {
            std::size_t q = p + 1;
            if (q < s_.size() && (s_[q] == '+' || s_[q] == '-')) ++q;
            if (q < s_.size() && isDigit(s_[q])) {
                while (q < s_.size() && isDigit(s_[q])) ++q;
                p = q;
            }
        }
        if (p < s_.size() && (isAlpha(s_[p]) || s_[p] == '.')) {
            return fail(start, "a valid number (digits, optional fraction and exponent)");
        }
        // std::from_chars does not accept a leading '.', so normalise ".5" to "0.5".
        std::string lexeme(s_.substr(start, p - start));
        if (lexeme.front() == '.') lexeme.insert(lexeme.begin(), '0');
        double value = 0.0;
        const auto res = std::from_chars(lexeme.data(), lexeme.data() + lexeme.size(), value);
        if (res.ec != std::errc() || res.ptr != lexeme.data() + lexeme.size()) {
            return fail(start, "a number within the range of a 64-bit floating point value");
        }
        pos_ = p;
        return makeNumber(value);
    }

    ExprPtr parsePrimary() {
        skipWs();
        const std::size_t start = pos_;
        const char c = peek();
        if (isDigit(c) || c == '.') return parseNumber();
        if (c == '(') {
            ++pos_;
            if (++recursion_ > kMaxExpressionDepth) {
                return fail(start, "a simpler expression (nesting depth is limited to " +
                                       std::to_string(kMaxExpressionDepth) + ")");
            }
            ExprPtr inner = parseOr();
            --recursion_;
            if (!inner) return nullptr;
            skipWs();
            if (peek() != ')') return fail(pos_, "')' or an operator");
            ++pos_;
            return inner;
        }
        if (isAlpha(c)) {
            std::size_t p = pos_;
            while (true) {
                while (p < s_.size() && (isAlpha(s_[p]) || isDigit(s_[p]))) ++p;
                if (p < s_.size() && s_[p] == '.') {
                    if (p + 1 < s_.size() && isAlpha(s_[p + 1])) {
                        ++p;
                        continue;
                    }
                    return fail(p + 1, "a name segment (letter or '_') after '.'");
                }
                break;
            }
            std::string name(s_.substr(start, p - start));
            pos_ = p;
            skipWs();
            if (peek() != '(') return makeIdentifier(std::move(name));
            return parseCall(name, start);
        }
        return fail(start, "a number, a parameter name, a function call, '(', '-' or '!'");
    }

    ExprPtr parseCall(const std::string& name, std::size_t nameStart) {
        const FunctionInfo* info = findFunction(name);
        if (!info) {
            return fail(nameStart,
                        "a known function (min, max, abs, floor, ceil, round, sqrt, clamp, if) before '('");
        }
        ++pos_; // '('
        if (++recursion_ > kMaxExpressionDepth) {
            return fail(nameStart, "a simpler expression (nesting depth is limited to " +
                                       std::to_string(kMaxExpressionDepth) + ")");
        }
        std::vector<ExprPtr> args;
        skipWs();
        if (peek() == ')') {
            return fail(pos_, "an argument for function '" + name + "'");
        }
        while (true) {
            ExprPtr arg = parseOr();
            if (!arg) return nullptr;
            args.push_back(std::move(arg));
            skipWs();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == ')') break;
            return fail(pos_, "',', ')' or an operator");
        }
        const int count = static_cast<int>(args.size());
        if (count < info->minArgs || (info->maxArgs >= 0 && count > info->maxArgs)) {
            std::string expected = "function '" + name + "' to be called with ";
            if (info->maxArgs < 0) expected += "at least " + std::to_string(info->minArgs) + " argument";
            else expected += "exactly " + std::to_string(info->minArgs) + " argument" + (info->minArgs == 1 ? "" : "s");
            expected += " (" + std::to_string(count) + " given)";
            return fail(pos_, std::move(expected));
        }
        ++pos_; // ')'
        --recursion_;
        return checked(makeCall(info->function, std::move(args)), nameStart);
    }

    std::string_view s_;
    std::size_t pos_ = 0;
    int recursion_ = 0;
    bool failed_ = false;
    ParseFailure failure_;
};

} // namespace

core::Result<ExprPtr> parseExpression(std::string_view text, ParseFailure* failure) {
    Parser parser(text);
    ExprPtr e = parser.run();
    if (e) return e;
    const ParseFailure& f = parser.failure();
    if (failure) *failure = f;
    std::string shown(text.substr(0, 120));
    if (text.size() > 120) shown += "...";
    return core::makeError(core::ErrorCode::ParseError,
                           "Parse error at position " + std::to_string(f.position) + " in '" + shown +
                               "': expected " + f.expected + " but found " + f.found + ".",
                           std::string(text.substr(0, 120)));
}

bool isFunctionName(std::string_view name) { return findFunction(name) != nullptr; }

const char* functionName(Function function) noexcept {
    for (const auto& f : kFunctions)
        if (f.function == function) return f.name;
    return "?";
}

bool isValidIdentifier(std::string_view name) {
    if (name.empty()) return false;
    bool segmentStart = true;
    for (const char c : name) {
        if (segmentStart) {
            if (!isAlpha(c)) return false;
            segmentStart = false;
        } else if (c == '.') {
            segmentStart = true;
        } else if (!isAlpha(c) && !isDigit(c)) {
            return false;
        }
    }
    return !segmentStart;
}

} // namespace fos::parametric
