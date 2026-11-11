// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// ParameterGraph: named parameters (constants or expressions), dependency tracking with cycle
/// detection, incremental recalculation, atomic transactions, bounds and constraints, change
/// notification, snapshots. UI-independent; values are doubles (mm, degrees, plain numbers, 0/1).
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fos/core/diagnostic.hpp"
#include "fos/core/result.hpp"
#include "fos/parametric/expression.hpp"

namespace fos::parametric {

/// Unit-kind tag of a parameter. Purely descriptive except Boolean, whose value must be 0 or 1.
enum class ParameterKind { Number, Length, Count, Angle, Ratio, Boolean };
const char* toString(ParameterKind kind) noexcept;
/// Inverse of toString(ParameterKind); std::nullopt for unknown text.
std::optional<ParameterKind> parameterKindFromString(std::string_view text) noexcept;

/// Complete, serialisable definition of one parameter.
struct ParameterDef {
    std::string name;        ///< Unique identifier, e.g. "cab.inner_width" (see isValidIdentifier).
    std::string expression;  ///< Expression text; empty means the parameter is the constant `value`.
    double value = 0.0;      ///< Constant value (mm / degrees / number). Ignored when `expression` is set.
    ParameterKind kind = ParameterKind::Number;
    std::string description;
    std::optional<double> lowerBound; ///< Inclusive; violated when value < lowerBound.
    std::optional<double> upperBound; ///< Inclusive; violated when value > upperBound.
    bool integer = false;    ///< Value must be a whole number (within 1e-9; it is then snapped).

    bool isExpression() const noexcept { return !expression.empty(); }

    static ParameterDef constant(std::string name, double value, ParameterKind kind = ParameterKind::Number);
    static ParameterDef formula(std::string name, std::string expression, ParameterKind kind = ParameterKind::Number);
};

enum class ConstraintSeverity { Warning, Error };

/// Named boolean condition over parameters. It is violated when `expression` evaluates to 0.
struct ConstraintDef {
    std::string name;        ///< Unique among constraints; free text (e.g. "shelf fits"), not empty.
    std::string expression;  ///< e.g. "cab.inner_width > 0".
    /// Human message. `{parameter.name}` placeholders are replaced by current values.
    std::string message;
    ConstraintSeverity severity = ConstraintSeverity::Error; ///< Error blocks the change; Warning is reported only.
};

/// Result of a successful change. All name lists are sorted ascending.
struct ChangeSet {
    std::vector<std::string> changed; ///< Parameters whose value is new or different (includes `added`).
    std::vector<std::string> added;   ///< Parameters that did not exist before.
    std::vector<std::string> removed; ///< Parameters that no longer exist.
    std::vector<std::pair<std::string, std::string>> renamed; ///< (old name, new name).
    core::Diagnostics warnings;       ///< Violated Warning-severity constraints among those re-checked.

    bool empty() const noexcept { return changed.empty() && added.empty() && removed.empty() && renamed.empty(); }
};

/// Statistics of the most recent recalculation (apply / single change / recalculateAll).
struct RecalcStats {
    std::vector<std::string> evaluated; ///< Expression parameters re-evaluated, in evaluation (topological) order.
    std::vector<std::string> changed;   ///< Parameters whose value changed, in evaluation order.
    std::size_t visited = 0;            ///< Parameters inspected (changed roots + transitive dependents).
    std::size_t constraintsChecked = 0; ///< Constraint expressions evaluated.
};

namespace detail {
struct GraphState;
struct Journal;
enum class Op { Define, Redefine, DefineOrReplace, SetValue, SetExpression, SetBounds, Remove, Rename,
                AddConstraint, RemoveConstraint };
/// One recorded transaction step (internal representation).
struct Step {
    Op op = Op::Define;
    ParameterDef def;         ///< Define/Redefine/DefineOrReplace; name + value/expression/bounds for setters.
    std::string newName;      ///< Rename target.
    ConstraintDef constraint; ///< AddConstraint / RemoveConstraint (name only).
};
} // namespace detail

/// Ordered list of changes that ParameterGraph::apply() executes atomically. A plain value object:
/// building it does not touch any graph. Steps run in order; references between parameters are
/// validated only after the last step, so definitions may appear before their dependencies.
class Transaction {
public:
    /// Adds a new parameter (fails at apply() if the name exists at that point).
    Transaction& define(ParameterDef def);
    /// Replaces the whole definition of an existing parameter.
    Transaction& redefine(ParameterDef def);
    /// Defines the parameter or replaces its definition if it exists.
    Transaction& defineOrReplace(ParameterDef def);
    /// Makes an existing parameter a constant with this value; metadata is kept.
    Transaction& setValue(std::string name, double value);
    /// Makes an existing parameter an expression; metadata is kept.
    Transaction& setExpression(std::string name, std::string expression);
    /// Sets or clears the inclusive bounds of an existing parameter.
    Transaction& setBounds(std::string name, std::optional<double> lower, std::optional<double> upper);
    Transaction& remove(std::string name);
    /// Renames a parameter and rewrites all expressions and constraints that reference it
    /// (rewritten texts are stored in canonical toString form).
    Transaction& rename(std::string oldName, std::string newName);
    Transaction& addConstraint(ConstraintDef constraint);
    Transaction& removeConstraint(std::string name);

    bool empty() const noexcept { return steps_.empty(); }
    std::size_t size() const noexcept { return steps_.size(); }

    /// Internal: the recorded steps (consumed by ParameterGraph::apply).
    const std::vector<detail::Step>& steps() const noexcept { return steps_; }

private:
    std::vector<detail::Step> steps_;
};

/// Immutable copy of a graph's complete state (definitions, values, constraints). O(1) to take.
class Snapshot {
public:
    Snapshot() = default;
    bool valid() const noexcept { return state_ != nullptr; }

private:
    friend class ParameterGraph;
    std::shared_ptr<const detail::GraphState> state_;
};

/// The dependency engine. Every mutating call is a transaction: it either succeeds completely or
/// leaves the graph exactly as it was and returns an error that explains why.
/// Copying a graph is cheap (state is shared copy-on-write); observers are not copied.
/// Not thread-safe.
class ParameterGraph {
public:
    using Observer = std::function<void(const ChangeSet&)>;

    ParameterGraph();
    ~ParameterGraph();
    ParameterGraph(const ParameterGraph& other);
    ParameterGraph& operator=(const ParameterGraph& other);
    ParameterGraph(ParameterGraph&&) noexcept;
    ParameterGraph& operator=(ParameterGraph&&) noexcept;

    /// Builds a graph from records in any order (definitions may precede their dependencies);
    /// everything is validated after the last record. Used for deserialisation.
    static core::Result<ParameterGraph> load(const std::vector<ParameterDef>& definitions,
                                             const std::vector<ConstraintDef>& constraints = {});

    // --- Mutation. Each call is an implicit transaction; see Transaction for semantics. ---
    core::Result<ChangeSet> apply(const Transaction& transaction);
    core::Result<ChangeSet> define(ParameterDef def);
    core::Result<ChangeSet> redefine(ParameterDef def);
    /// Convenience: define a constant / an expression parameter with default metadata.
    core::Result<ChangeSet> defineConstant(std::string name, double value, ParameterKind kind = ParameterKind::Number);
    core::Result<ChangeSet> defineExpression(std::string name, std::string expression,
                                             ParameterKind kind = ParameterKind::Number);
    core::Result<ChangeSet> setValue(std::string name, double value);
    core::Result<ChangeSet> setExpression(std::string name, std::string expression);
    core::Result<ChangeSet> setBounds(std::string name, std::optional<double> lower, std::optional<double> upper);
    /// Fails (naming the users) if other parameters or constraints reference the parameter.
    core::Result<ChangeSet> remove(std::string name);
    core::Result<ChangeSet> rename(std::string oldName, std::string newName);
    core::Result<ChangeSet> addConstraint(ConstraintDef constraint);
    core::Result<ChangeSet> removeConstraint(std::string name);

    /// Re-evaluates every expression and re-checks every bound and constraint. On a valid graph this
    /// changes nothing (idempotent); it exists as a self-check and for tests.
    core::Result<ChangeSet> recalculateAll();

    // --- Queries ---
    bool contains(std::string_view name) const;
    std::size_t size() const;
    /// Current value; NotFound error if the parameter does not exist.
    core::Result<double> value(std::string_view name) const;
    /// Definition record, or nullptr. The pointer is invalidated by the next mutation.
    const ParameterDef* find(std::string_view name) const;
    /// All parameter names sorted ascending (byte-wise).
    std::vector<std::string> names() const;
    /// All definitions sorted by name; sufficient to rebuild the graph with load().
    std::vector<ParameterDef> definitions() const;
    /// All constraints sorted by name.
    std::vector<ConstraintDef> constraints() const;
    /// Parameters directly referenced by `name`'s expression (sorted; empty for constants/unknown).
    std::vector<std::string> dependencies(std::string_view name) const;
    /// Parameters whose expressions directly reference `name` (sorted).
    std::vector<std::string> dependents(std::string_view name) const;
    /// All direct and indirect dependents of `name` in evaluation order.
    std::vector<std::string> transitiveDependents(std::string_view name) const;
    /// All parameters in a deterministic topological order (dependencies before dependents).
    std::vector<std::string> evaluationOrder() const;
    /// Evaluates an ad-hoc expression against the current values without changing the graph.
    core::Result<double> evaluate(std::string_view expression) const;
    /// Evaluates all constraints now: one diagnostic per violated constraint (Warning or Error severity).
    core::Diagnostics checkConstraints() const;

    /// Statistics of the last recalculation attempt (also filled when the change was rolled back).
    const RecalcStats& lastRecalculation() const noexcept { return stats_; }
    /// All findings of the last mutation attempt: errors that caused a rollback and warnings.
    const core::Diagnostics& lastDiagnostics() const noexcept { return diagnostics_; }

    // --- Change notification ---
    /// Observers are called after every successful mutation and restore(), with the ChangeSet.
    std::size_t addObserver(Observer observer);
    void removeObserver(std::size_t id);

    // --- Undo / redo support ---
    Snapshot snapshot() const;
    /// Replaces the complete state by the snapshot; returns (and notifies) the differences.
    ChangeSet restore(const Snapshot& snapshot);

private:
    void ensureUnique();
    core::Result<ChangeSet> run(const std::vector<detail::Step>& steps, bool recalculateEverything);
    void notify(const ChangeSet& changes);

    std::shared_ptr<detail::GraphState> state_;
    RecalcStats stats_;
    core::Diagnostics diagnostics_;
    std::vector<std::pair<std::size_t, Observer>> observers_;
    std::size_t nextObserverId_ = 1;
};

} // namespace fos::parametric
