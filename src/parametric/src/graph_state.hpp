// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Private implementation types of ParameterGraph (not installed, not part of the public API).
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "fos/parametric/parameter_graph.hpp"

namespace fos::parametric::detail {

/// A parameter. `def.value` always holds the current value (also for expression parameters).
struct Node {
    ParameterDef def;
    ExprPtr expr;                  ///< Null for constants.
    std::vector<std::string> deps; ///< Sorted unique identifiers referenced by `expr`.
};

struct ConstraintNode {
    ConstraintDef def;
    ExprPtr expr;
    std::vector<std::string> deps;
};

/// Complete graph state. Ordered containers only, so every traversal is deterministic.
struct GraphState {
    std::map<std::string, Node, std::less<>> nodes;
    /// Reverse edges: referenced name -> parameters whose expressions reference it. During a
    /// transaction a key may name a parameter that does not exist (yet).
    std::map<std::string, std::set<std::string>, std::less<>> dependents;
    std::map<std::string, ConstraintNode, std::less<>> constraints;
};

/// Undo log of one transaction: enough to restore the state exactly.
struct Journal {
    std::map<std::string, std::optional<Node>> nodesBefore;                 ///< First-touch copies.
    std::map<std::string, std::optional<ConstraintNode>> constraintsBefore; ///< First-touch copies.
    std::vector<std::pair<Node*, double>> valuesBefore; ///< Values overwritten by recalculation.
    std::vector<std::pair<std::string, std::string>> renames;
};

// --- graph_state.cpp: structural edits ---
void putNode(GraphState& state, const std::string& name, std::optional<Node> node, Journal* journal);
void putConstraint(GraphState& state, const std::string& name, std::optional<ConstraintNode> node, Journal* journal);
void rollback(GraphState& state, Journal& journal);
core::Result<void> applyStep(GraphState& state, const Step& step, Journal& journal);
std::string quoteList(const std::vector<std::string>& names, std::size_t limit = 8);

// --- graph_commit.cpp: validation and recalculation ---
/// Parameters reachable from `roots` through dependents, dependencies first (deterministic).
std::vector<std::string_view> orderFrom(const GraphState& state, const std::vector<std::string_view>& roots);
core::Result<void> validateAndRecalculate(GraphState& state, Journal& journal, bool all, RecalcStats& stats,
                                          core::Diagnostics& diagnostics);
ChangeSet buildChangeSet(const GraphState& state, const Journal& journal);
/// Appends one diagnostic per violated constraint in `names` (or all when `names` is null).
core::Result<void> checkConstraints(const GraphState& state, const std::vector<std::string_view>* names,
                                    core::Diagnostics& diagnostics, std::size_t* checked);

} // namespace fos::parametric::detail
