// SPDX-License-Identifier: AGPL-3.0-only
#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "graph_state.hpp"

namespace fos::parametric::detail {
namespace {

using core::ErrorCode;
using core::makeError;

constexpr double kIntegerTolerance = 1e-9;

core::Error record(core::Diagnostics& diagnostics, ErrorCode code, const char* diagnosticCode, std::string message,
                   std::string entity) {
    diagnostics.push_back({core::Severity::Error, diagnosticCode, message, entity});
    return makeError(code, std::move(message), std::move(entity));
}

Lookup lookupIn(const GraphState& state) {
    return [&state](const std::string& name) -> std::optional<double> {
        const auto it = state.nodes.find(name);
        if (it == state.nodes.end()) return std::nullopt;
        return it->second.def.value;
    };
}

bool removedInTransaction(const GraphState& state, const Journal& journal, const std::string& name) {
    const auto it = journal.nodesBefore.find(name);
    return it != journal.nodesBefore.end() && it->second.has_value() && !state.nodes.count(name);
}

/// Checks that nothing references a missing parameter.
core::Result<void> validateReferences(const GraphState& state, const Journal& journal, core::Diagnostics& diags) {
    bool anyRemoved = false;
    for (const auto& [name, before] : journal.nodesBefore) {
        if (!before || state.nodes.count(name)) continue;
        anyRemoved = true;
        const auto users = state.dependents.find(name);
        if (users != state.dependents.end() && !users->second.empty()) {
            const std::vector<std::string> list(users->second.begin(), users->second.end());
            return record(diags, ErrorCode::InvalidArgument, "parametric.parameter.in_use",
                          "Cannot remove parameter '" + name + "': it is used by " + quoteList(list) + ".", name);
        }
    }
    for (const auto& [name, before] : journal.nodesBefore) {
        const auto it = state.nodes.find(name);
        if (it == state.nodes.end()) continue;
        for (const auto& dep : it->second.deps) {
            if (state.nodes.count(dep)) continue;
            return record(diags, ErrorCode::NotFound, "parametric.reference.undefined",
                          "Parameter '" + name + "' refers to undefined parameter '" + dep + "' in expression '" +
                              it->second.def.expression + "'.",
                          dep);
        }
    }
    if (!anyRemoved && journal.constraintsBefore.empty()) return {};
    for (const auto& [name, constraint] : state.constraints) {
        if (!anyRemoved && !journal.constraintsBefore.count(name)) continue;
        for (const auto& dep : constraint.deps) {
            if (state.nodes.count(dep)) continue;
            if (removedInTransaction(state, journal, dep)) {
                return record(diags, ErrorCode::InvalidArgument, "parametric.parameter.in_use",
                              "Cannot remove parameter '" + dep + "': it is used by constraint '" + name + "'.", dep);
            }
            return record(diags, ErrorCode::NotFound, "parametric.reference.undefined",
                          "Constraint '" + name + "' refers to undefined parameter '" + dep + "' in expression '" +
                              constraint.def.expression + "'.",
                          dep);
        }
    }
    return {};
}

/// The state before the transaction was acyclic, so every new cycle passes through a parameter
/// whose definition was touched. Iterative three-colour DFS along dependency edges from those.
core::Result<void> detectCycles(const GraphState& state, const Journal& journal, core::Diagnostics& diags) {
    enum : char { White = 0, Grey = 1, Black = 2 };
    std::unordered_map<std::string_view, char> colour;
    struct Frame {
        const Node* node;
        std::size_t next;
    };
    std::vector<Frame> stack;
    for (const auto& [startName, before] : journal.nodesBefore) {
        const auto start = state.nodes.find(startName);
        if (start == state.nodes.end() || start->second.deps.empty()) continue;
        if (colour[start->first] != White) continue;
        colour[start->first] = Grey;
        stack.push_back({&start->second, 0});
        while (!stack.empty()) {
            Frame& top = stack.back();
            if (top.next == top.node->deps.size()) {
                colour[top.node->def.name] = Black;
                stack.pop_back();
                continue;
            }
            const auto child = state.nodes.find(top.node->deps[top.next++]);
            if (child == state.nodes.end()) continue; // reported by validateReferences
            char& c = colour[child->first];
            if (c == White) {
                c = Grey;
                stack.push_back({&child->second, 0});
            } else if (c == Grey) {
                std::size_t from = 0;
                while (stack[from].node != &child->second) ++from;
                std::string path;
                for (std::size_t i = from; i < stack.size(); ++i) path += stack[i].node->def.name + " -> ";
                path += child->first;
                const Node& culprit = *stack.back().node;
                return record(diags, ErrorCode::CyclicDependency, "parametric.dependency.cycle",
                              "Cyclic dependency: " + path + " (each parameter depends on the next); parameter '" +
                                  culprit.def.name + "' is defined as '" + culprit.def.expression + "'.",
                              culprit.def.name);
            }
        }
    }
    return {};
}

std::string origin(const Node& node) {
    return node.expr ? " (computed from '" + node.def.expression + "')" : std::string();
}

/// Integer / boolean / bound checks; may snap `value` to the nearest integer.
core::Result<void> checkValue(const Node& node, double& value, core::Diagnostics& diags) {
    const std::string& name = node.def.name;
    if (node.def.integer) {
        const double rounded = std::round(value);
        if (std::fabs(value - rounded) > kIntegerTolerance) {
            return record(diags, ErrorCode::ConstraintViolation, "parametric.parameter.not_integer",
                          "Parameter '" + name + "' must be a whole number but its value is " +
                              formatNumberForMessage(value) + origin(node) + ".",
                          name);
        }
        value = rounded;
    }
    if (node.def.kind == ParameterKind::Boolean && value != 0.0 && value != 1.0) {
        return record(diags, ErrorCode::ConstraintViolation, "parametric.parameter.not_boolean",
                      "Boolean parameter '" + name + "' must be 0 or 1 but its value is " +
                          formatNumberForMessage(value) + origin(node) + ".",
                      name);
    }
    if (node.def.lowerBound && value < *node.def.lowerBound) {
        return record(diags, ErrorCode::ConstraintViolation, "parametric.parameter.out_of_bounds",
                      "Parameter '" + name + "' = " + formatNumberForMessage(value) + " is below its lower bound " +
                          formatNumberForMessage(*node.def.lowerBound) + origin(node) + ".",
                      name);
    }
    if (node.def.upperBound && value > *node.def.upperBound) {
        return record(diags, ErrorCode::ConstraintViolation, "parametric.parameter.out_of_bounds",
                      "Parameter '" + name + "' = " + formatNumberForMessage(value) + " is above its upper bound " +
                          formatNumberForMessage(*node.def.upperBound) + origin(node) + ".",
                      name);
    }
    return {};
}

std::string fillTemplate(const GraphState& state, const std::string& text) {
    std::string out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t open = text.find('{', pos);
        const std::size_t close = open == std::string::npos ? open : text.find('}', open);
        if (close == std::string::npos) break;
        out.append(text, pos, open - pos);
        const auto it = state.nodes.find(std::string_view(text).substr(open + 1, close - open - 1));
        if (it != state.nodes.end()) out += formatNumberForMessage(it->second.def.value);
        else out.append(text, open, close - open + 1);
        pos = close + 1;
    }
    out.append(text, pos, std::string::npos);
    return out;
}

/// Evaluates one constraint; appends a diagnostic when violated. Evaluation failures are errors.
core::Result<void> checkOne(const GraphState& state, const ConstraintNode& c, core::Diagnostics& diags) {
    const auto result = evaluate(*c.expr, lookupIn(state));
    if (!result) {
        return record(diags, ErrorCode::InvalidArgument, "parametric.constraint.evaluation_failed",
                      "Constraint '" + c.def.name + "' ('" + c.def.expression + "') cannot be evaluated: " +
                          result.error().message + ".",
                      c.def.name);
    }
    if (result.value() != 0.0) return {};
    std::string values;
    for (const auto& dep : c.deps) {
        const auto it = state.nodes.find(dep);
        if (it == state.nodes.end()) continue;
        values += (values.empty() ? "" : ", ") + dep + " = " + formatNumberForMessage(it->second.def.value);
    }
    std::string message = "Constraint '" + c.def.name + "' violated: ";
    if (!c.def.message.empty()) message += fillTemplate(state, c.def.message) + " ";
    message += "(" + c.def.expression + " is false";
    if (!values.empty()) message += "; " + values;
    message += ").";
    const bool isError = c.def.severity == ConstraintSeverity::Error;
    diags.push_back({isError ? core::Severity::Error : core::Severity::Warning, "parametric.constraint.violated",
                     std::move(message), c.def.name});
    return {};
}

} // namespace

std::vector<std::string_view> orderFrom(const GraphState& state, const std::vector<std::string_view>& roots) {
    static const std::set<std::string> kNone;
    struct Frame {
        std::string_view name;
        std::set<std::string>::const_iterator next, end;
    };
    std::unordered_set<std::string_view> visited;
    std::vector<std::string_view> postorder;
    std::vector<Frame> stack;
    const auto push = [&](std::string_view name) {
        const auto node = state.nodes.find(name);
        if (node == state.nodes.end() || !visited.insert(node->first).second) return;
        const auto users = state.dependents.find(name);
        const std::set<std::string>& set = users != state.dependents.end() ? users->second : kNone;
        stack.push_back({node->first, set.begin(), set.end()});
    };
    for (const auto& root : roots) {
        push(root);
        while (!stack.empty()) {
            Frame& top = stack.back();
            if (top.next == top.end) {
                postorder.push_back(top.name);
                stack.pop_back();
            } else {
                const std::string& child = *top.next++;
                push(child); // may invalidate `top`
            }
        }
    }
    std::reverse(postorder.begin(), postorder.end());
    return postorder;
}

core::Result<void> checkConstraints(const GraphState& state, const std::vector<std::string_view>* names,
                                    core::Diagnostics& diagnostics, std::size_t* checked) {
    core::Result<void> first;
    const auto visit = [&](const ConstraintNode& c) {
        if (checked) ++*checked;
        auto r = checkOne(state, c, diagnostics);
        if (!r && first) first = r;
    };
    if (names) {
        for (const auto& name : *names) visit(state.constraints.find(name)->second);
    } else {
        for (const auto& [name, c] : state.constraints) visit(c);
    }
    if (!first) return first;
    for (const auto& d : diagnostics) {
        if (d.severity != core::Severity::Error) continue;
        std::size_t errors = 0;
        for (const auto& other : diagnostics) errors += other.severity == core::Severity::Error ? 1 : 0;
        std::string message = d.message;
        if (errors > 1) message += " (" + std::to_string(errors - 1) + " more violation(s), see diagnostics.)";
        return makeError(ErrorCode::ConstraintViolation, std::move(message), d.entityId);
    }
    return {};
}

core::Result<void> validateAndRecalculate(GraphState& state, Journal& journal, bool all, RecalcStats& stats,
                                          core::Diagnostics& diagnostics) {
    if (auto r = validateReferences(state, journal, diagnostics); !r) return r;
    if (auto r = detectCycles(state, journal, diagnostics); !r) return r;

    std::vector<std::string_view> roots;
    if (all) {
        for (const auto& [name, node] : state.nodes) roots.push_back(name);
    } else {
        for (const auto& [name, before] : journal.nodesBefore) {
            if (const auto it = state.nodes.find(name); it != state.nodes.end()) roots.push_back(it->first);
        }
    }
    const std::vector<std::string_view> order = orderFrom(state, roots);
    stats.visited = order.size();

    const Lookup lookup = lookupIn(state);
    std::unordered_set<std::string_view> changed; // membership tests only (never iterated)
    for (const std::string_view name : order) {
        Node& node = state.nodes.find(name)->second;
        const auto touched = journal.nodesBefore.find(node.def.name);
        const bool isTouched = touched != journal.nodesBefore.end();
        bool needed = all || isTouched;
        for (std::size_t i = 0; !needed && i < node.deps.size(); ++i) needed = changed.count(node.deps[i]) > 0;
        if (!needed) continue; // early cut-off: no input changed

        double value = node.def.value;
        if (node.expr) {
            stats.evaluated.emplace_back(name);
            const auto result = evaluate(*node.expr, lookup);
            if (!result) {
                return record(diagnostics, result.error().code, "parametric.evaluation.failed",
                              "Parameter '" + node.def.name + "' ('" + node.def.expression +
                                  "') cannot be evaluated: " + result.error().message + ".",
                              node.def.name);
            }
            value = result.value();
        }
        if (auto r = checkValue(node, value, diagnostics); !r) return r;

        bool isChanged;
        if (isTouched) {
            isChanged = !touched->second || touched->second->def.value != value;
        } else {
            isChanged = node.def.value != value;
            if (isChanged) journal.valuesBefore.emplace_back(&node, node.def.value);
        }
        node.def.value = value;
        if (isChanged) {
            changed.insert(name);
            stats.changed.emplace_back(name);
        }
    }

    // Constraints: re-check those that were added/rewritten or that read a changed value.
    if (all) return checkConstraints(state, nullptr, diagnostics, &stats.constraintsChecked);
    std::vector<std::string_view> affected;
    for (const auto& [name, c] : state.constraints) {
        bool needed = journal.constraintsBefore.count(name) > 0;
        for (std::size_t i = 0; !needed && i < c.deps.size(); ++i) needed = changed.count(c.deps[i]) > 0;
        if (needed) affected.push_back(name);
    }
    return checkConstraints(state, &affected, diagnostics, &stats.constraintsChecked);
}

ChangeSet buildChangeSet(const GraphState& state, const Journal& journal) {
    ChangeSet cs;
    for (const auto& [name, before] : journal.nodesBefore) {
        const auto now = state.nodes.find(name);
        const bool exists = now != state.nodes.end();
        if (!before && exists) {
            cs.added.push_back(name);
            cs.changed.push_back(name);
        } else if (before && !exists) {
            cs.removed.push_back(name);
        } else if (before && exists && before->def.value != now->second.def.value) {
            cs.changed.push_back(name);
        }
    }
    for (const auto& [node, oldValue] : journal.valuesBefore) {
        if (node->def.value != oldValue) cs.changed.push_back(node->def.name);
    }
    const auto erase = [](std::vector<std::string>& v, const std::string& name) {
        const auto it = std::find(v.begin(), v.end(), name);
        if (it == v.end()) return false;
        v.erase(it);
        return true;
    };
    for (const auto& [oldName, newName] : journal.renames) {
        const auto before = journal.nodesBefore.find(oldName);
        const auto now = state.nodes.find(newName);
        if (oldName == newName || before == journal.nodesBefore.end() || !before->second ||
            state.nodes.count(oldName) || now == state.nodes.end()) {
            continue;
        }
        const auto target = journal.nodesBefore.find(newName);
        if (target != journal.nodesBefore.end() && target->second) continue; // new name replaced a removed one
        erase(cs.removed, oldName);
        erase(cs.added, newName);
        if (before->second->def.value == now->second.def.value) erase(cs.changed, newName);
        cs.renamed.emplace_back(oldName, newName);
    }
    std::sort(cs.changed.begin(), cs.changed.end());
    std::sort(cs.added.begin(), cs.added.end());
    std::sort(cs.removed.begin(), cs.removed.end());
    std::sort(cs.renamed.begin(), cs.renamed.end());
    return cs;
}

} // namespace fos::parametric::detail
