// SPDX-License-Identifier: AGPL-3.0-only
#include <algorithm>
#include <cmath>

#include "graph_state.hpp"

namespace fos::parametric::detail {
namespace {

using core::ErrorCode;
using core::makeError;

void unlink(GraphState& state, const Node& node) {
    for (const auto& dep : node.deps) {
        auto it = state.dependents.find(dep);
        if (it == state.dependents.end()) continue;
        it->second.erase(node.def.name);
        if (it->second.empty()) state.dependents.erase(it);
    }
}

void link(GraphState& state, const Node& node) {
    for (const auto& dep : node.deps) state.dependents[dep].insert(node.def.name);
}

core::Result<void> validateName(const std::string& name) {
    if (!isValidIdentifier(name)) {
        return makeError(ErrorCode::InvalidArgument,
                         "'" + name + "' is not a valid parameter name: use letters, digits and '_' in segments "
                         "separated by '.', each segment starting with a letter or '_' (e.g. 'cab.inner_width').",
                         name);
    }
    if (isFunctionName(name)) {
        return makeError(ErrorCode::InvalidArgument,
                         "'" + name + "' cannot be used as a parameter name because it is a built-in function.", name);
    }
    return {};
}

/// Validates a definition, parses its expression and stores it (keeping the current value of an
/// existing expression parameter so that change detection compares against it).
core::Result<void> putDefinition(GraphState& state, ParameterDef def, Journal& journal) {
    if (auto r = validateName(def.name); !r) return r;
    for (const auto& bound : {def.lowerBound, def.upperBound}) {
        if (bound && !std::isfinite(*bound)) {
            return makeError(ErrorCode::InvalidArgument,
                             "Parameter '" + def.name + "' has a bound that is not a finite number.", def.name);
        }
    }
    if (def.lowerBound && def.upperBound && *def.lowerBound > *def.upperBound) {
        return makeError(ErrorCode::InvalidArgument,
                         "Parameter '" + def.name + "' has lower bound " + formatNumberForMessage(*def.lowerBound) +
                             " above its upper bound " + formatNumberForMessage(*def.upperBound) + ".",
                         def.name);
    }
    Node node;
    if (def.isExpression()) {
        auto parsed = parseExpression(def.expression);
        if (!parsed) {
            return makeError(ErrorCode::ParseError, "Parameter '" + def.name + "': " + parsed.error().message,
                             def.name);
        }
        node.expr = parsed.value();
        node.deps = referencedIdentifiers(*node.expr);
        const auto existing = state.nodes.find(def.name);
        def.value = existing != state.nodes.end() ? existing->second.def.value : 0.0;
    } else if (!std::isfinite(def.value)) {
        return makeError(ErrorCode::InvalidArgument,
                         "Parameter '" + def.name + "' cannot be set to a value that is not a finite number.",
                         def.name);
    }
    const std::string name = def.name;
    node.def = std::move(def);
    putNode(state, name, std::move(node), &journal);
    return {};
}

core::Error notFound(const std::string& name) {
    return makeError(ErrorCode::NotFound, "Parameter '" + name + "' is not defined.", name);
}

void replaceAll(std::string& text, const std::string& from, const std::string& to) {
    for (std::size_t pos = text.find(from); pos != std::string::npos; pos = text.find(from, pos + to.size())) {
        text.replace(pos, from.size(), to);
    }
}

core::Result<void> renameParameter(GraphState& state, const std::string& oldName, const std::string& newName,
                                   Journal& journal) {
    const auto it = state.nodes.find(oldName);
    if (it == state.nodes.end()) return notFound(oldName);
    if (auto r = validateName(newName); !r) return r;
    if (oldName == newName) return {};
    if (state.nodes.count(newName)) {
        return makeError(ErrorCode::AlreadyExists,
                         "Cannot rename parameter '" + oldName + "' to '" + newName +
                             "': a parameter with that name already exists.",
                         newName);
    }
    const auto renameFn = [&](const std::string& id) { return id == oldName ? newName : id; };

    Node moved = it->second;
    std::vector<std::string> users;
    if (const auto d = state.dependents.find(oldName); d != state.dependents.end()) {
        users.assign(d->second.begin(), d->second.end());
    }
    putNode(state, oldName, std::nullopt, &journal);
    moved.def.name = newName;
    putNode(state, newName, std::move(moved), &journal);

    for (const auto& user : users) {
        const auto u = state.nodes.find(user);
        if (u == state.nodes.end()) continue;
        Node rewritten = u->second;
        rewritten.expr = renameIdentifiers(rewritten.expr, renameFn);
        rewritten.def.expression = toString(*rewritten.expr);
        rewritten.deps = referencedIdentifiers(*rewritten.expr);
        putNode(state, user, std::move(rewritten), &journal);
    }
    std::vector<std::string> constraintNames;
    for (const auto& [name, c] : state.constraints) {
        if (std::binary_search(c.deps.begin(), c.deps.end(), oldName)) constraintNames.push_back(name);
    }
    for (const auto& name : constraintNames) {
        ConstraintNode rewritten = state.constraints.find(name)->second;
        rewritten.expr = renameIdentifiers(rewritten.expr, renameFn);
        rewritten.def.expression = toString(*rewritten.expr);
        rewritten.deps = referencedIdentifiers(*rewritten.expr);
        replaceAll(rewritten.def.message, "{" + oldName + "}", "{" + newName + "}");
        putConstraint(state, name, std::move(rewritten), &journal);
    }
    // Collapse chains (a -> b, b -> c  =>  a -> c) so the change set reports the net effect.
    for (auto& pair : journal.renames) {
        if (pair.second == oldName) {
            pair.second = newName;
            return {};
        }
    }
    journal.renames.emplace_back(oldName, newName);
    return {};
}

} // namespace

std::string quoteList(const std::vector<std::string>& names, std::size_t limit) {
    std::string out;
    for (std::size_t i = 0; i < names.size() && i < limit; ++i) {
        if (i) out += ", ";
        out += "'" + names[i] + "'";
    }
    if (names.size() > limit) out += " and " + std::to_string(names.size() - limit) + " more";
    return out;
}

void putNode(GraphState& state, const std::string& name, std::optional<Node> node, Journal* journal) {
    const auto it = state.nodes.find(name);
    if (journal && !journal->nodesBefore.count(name)) {
        journal->nodesBefore.emplace(name, it != state.nodes.end() ? std::optional<Node>(it->second) : std::nullopt);
    }
    if (it != state.nodes.end()) {
        unlink(state, it->second);
        if (node) it->second = std::move(*node);
        else state.nodes.erase(it);
    } else if (node) {
        state.nodes.emplace(name, std::move(*node));
    }
    if (const auto now = state.nodes.find(name); now != state.nodes.end()) link(state, now->second);
}

void putConstraint(GraphState& state, const std::string& name, std::optional<ConstraintNode> node,
                   Journal* journal) {
    const auto it = state.constraints.find(name);
    if (journal && !journal->constraintsBefore.count(name)) {
        journal->constraintsBefore.emplace(
            name, it != state.constraints.end() ? std::optional<ConstraintNode>(it->second) : std::nullopt);
    }
    if (it != state.constraints.end()) state.constraints.erase(it);
    if (node) state.constraints.emplace(name, std::move(*node));
}

void rollback(GraphState& state, Journal& journal) {
    // Values first: the recorded Node pointers are still valid because recalculation runs after
    // the last structural edit of a transaction.
    for (auto it = journal.valuesBefore.rbegin(); it != journal.valuesBefore.rend(); ++it) {
        it->first->def.value = it->second;
    }
    for (auto& [name, before] : journal.nodesBefore) putNode(state, name, std::move(before), nullptr);
    for (auto& [name, before] : journal.constraintsBefore) putConstraint(state, name, std::move(before), nullptr);
    journal = Journal{};
}

core::Result<void> applyStep(GraphState& state, const Step& step, Journal& journal) {
    const std::string& name = step.def.name;
    const auto existing = state.nodes.find(name);
    const bool exists = existing != state.nodes.end();
    switch (step.op) {
    case Op::Define:
        if (exists) {
            return makeError(ErrorCode::AlreadyExists, "Parameter '" + name + "' is already defined.", name);
        }
        return putDefinition(state, step.def, journal);
    case Op::Redefine:
        if (!exists) return notFound(name);
        return putDefinition(state, step.def, journal);
    case Op::DefineOrReplace: return putDefinition(state, step.def, journal);
    case Op::SetValue: {
        if (!exists) return notFound(name);
        ParameterDef def = existing->second.def;
        def.expression.clear();
        def.value = step.def.value;
        return putDefinition(state, std::move(def), journal);
    }
    case Op::SetExpression: {
        if (!exists) return notFound(name);
        if (step.def.expression.empty()) {
            return makeError(ErrorCode::ParseError,
                             "Parameter '" + name + "': the expression is empty; use setValue for a constant.", name);
        }
        ParameterDef def = existing->second.def;
        def.expression = step.def.expression;
        return putDefinition(state, std::move(def), journal);
    }
    case Op::SetBounds: {
        if (!exists) return notFound(name);
        ParameterDef def = existing->second.def;
        def.lowerBound = step.def.lowerBound;
        def.upperBound = step.def.upperBound;
        return putDefinition(state, std::move(def), journal);
    }
    case Op::Remove:
        if (!exists) return notFound(name);
        putNode(state, name, std::nullopt, &journal);
        return {};
    case Op::Rename: return renameParameter(state, name, step.newName, journal);
    case Op::AddConstraint: {
        const ConstraintDef& c = step.constraint;
        if (c.name.empty()) {
            return makeError(ErrorCode::InvalidArgument, "A constraint must have a non-empty name.");
        }
        if (state.constraints.count(c.name)) {
            return makeError(ErrorCode::AlreadyExists, "Constraint '" + c.name + "' is already defined.", c.name);
        }
        auto parsed = parseExpression(c.expression);
        if (!parsed) {
            return makeError(ErrorCode::ParseError, "Constraint '" + c.name + "': " + parsed.error().message, c.name);
        }
        ConstraintNode node;
        node.def = c;
        node.expr = parsed.value();
        node.deps = referencedIdentifiers(*node.expr);
        putConstraint(state, c.name, std::move(node), &journal);
        return {};
    }
    case Op::RemoveConstraint:
        if (!state.constraints.count(step.constraint.name)) {
            return makeError(ErrorCode::NotFound, "Constraint '" + step.constraint.name + "' is not defined.",
                             step.constraint.name);
        }
        putConstraint(state, step.constraint.name, std::nullopt, &journal);
        return {};
    }
    return makeError(ErrorCode::Internal, "Unknown transaction step.");
}

} // namespace fos::parametric::detail
