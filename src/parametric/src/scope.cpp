// SPDX-License-Identifier: AGPL-3.0-only
#include <set>

#include "fos/parametric/scope.hpp"

namespace fos::parametric {
namespace {

struct Renamer {
    std::string prefix;
    std::set<std::string, std::less<>> locals;

    std::string operator()(const std::string& id) const {
        const std::string outer = std::string(kOuterScope) + ".";
        if (id.compare(0, outer.size(), outer) == 0) return id.substr(outer.size());
        if (locals.count(id)) return qualify(prefix, id);
        return id;
    }
};

core::Result<std::string> rewrite(const std::string& expression, const Renamer& renamer, const std::string& owner) {
    auto parsed = parseExpression(expression);
    if (!parsed) {
        return core::makeError(core::ErrorCode::ParseError, owner + ": " + parsed.error().message, owner);
    }
    return toString(*renameIdentifiers(parsed.value(), renamer));
}

std::string rewriteMessage(const std::string& text, const Renamer& renamer) {
    std::string out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t open = text.find('{', pos);
        const std::size_t close = open == std::string::npos ? open : text.find('}', open);
        if (close == std::string::npos) break;
        out.append(text, pos, open - pos + 1);
        const std::string inner = text.substr(open + 1, close - open - 1);
        out += isValidIdentifier(inner) ? renamer(inner) : inner;
        out += '}';
        pos = close + 1;
    }
    out.append(text, pos, std::string::npos);
    return out;
}

} // namespace

std::string qualify(std::string_view prefix, std::string_view name) {
    if (prefix.empty()) return std::string(name);
    std::string out(prefix);
    out += '.';
    out += name;
    return out;
}

core::Result<ParameterTemplate> instantiate(const ParameterTemplate& tmpl, std::string_view prefix) {
    if (!isValidIdentifier(prefix)) {
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "'" + std::string(prefix) + "' is not a valid scope prefix (expected a name such as "
                               "'cab1' or 'cab1.shelf').",
                               std::string(prefix));
    }
    Renamer renamer;
    renamer.prefix = std::string(prefix);
    for (const auto& p : tmpl.parameters) renamer.locals.insert(p.name);

    ParameterTemplate out;
    for (const auto& p : tmpl.parameters) {
        ParameterDef def = p;
        def.name = qualify(prefix, p.name);
        if (p.isExpression()) {
            auto text = rewrite(p.expression, renamer, "Parameter '" + p.name + "'");
            if (!text) return text.error();
            def.expression = text.value();
        }
        out.parameters.push_back(std::move(def));
    }
    for (const auto& c : tmpl.constraints) {
        ConstraintDef def = c;
        def.name = qualify(prefix, c.name);
        auto text = rewrite(c.expression, renamer, "Constraint '" + c.name + "'");
        if (!text) return text.error();
        def.expression = text.value();
        def.message = rewriteMessage(c.message, renamer);
        out.constraints.push_back(std::move(def));
    }
    return out;
}

void appendTo(Transaction& transaction, const ParameterTemplate& tmpl) {
    for (const auto& p : tmpl.parameters) transaction.define(p);
    for (const auto& c : tmpl.constraints) transaction.addConstraint(c);
}

ParameterTemplate merge(ParameterTemplate a, const ParameterTemplate& b) {
    a.parameters.insert(a.parameters.end(), b.parameters.begin(), b.parameters.end());
    a.constraints.insert(a.constraints.end(), b.constraints.begin(), b.constraints.end());
    return a;
}

} // namespace fos::parametric
