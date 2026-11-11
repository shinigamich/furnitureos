// SPDX-License-Identifier: AGPL-3.0-only
#include <algorithm>

#include "graph_state.hpp"

namespace fos::parametric {

using detail::GraphState;
using detail::Op;
using detail::Step;

const char* toString(ParameterKind kind) noexcept {
    switch (kind) {
    case ParameterKind::Number: return "Number";
    case ParameterKind::Length: return "Length";
    case ParameterKind::Count: return "Count";
    case ParameterKind::Angle: return "Angle";
    case ParameterKind::Ratio: return "Ratio";
    case ParameterKind::Boolean: return "Boolean";
    }
    return "Number";
}

std::optional<ParameterKind> parameterKindFromString(std::string_view text) noexcept {
    for (const ParameterKind kind : {ParameterKind::Number, ParameterKind::Length, ParameterKind::Count,
                                     ParameterKind::Angle, ParameterKind::Ratio, ParameterKind::Boolean}) {
        if (text == toString(kind)) return kind;
    }
    return std::nullopt;
}

ParameterDef ParameterDef::constant(std::string name, double value, ParameterKind kind) {
    ParameterDef def;
    def.name = std::move(name);
    def.value = value;
    def.kind = kind;
    return def;
}

ParameterDef ParameterDef::formula(std::string name, std::string expression, ParameterKind kind) {
    ParameterDef def;
    def.name = std::move(name);
    def.expression = std::move(expression);
    def.kind = kind;
    return def;
}

// --- Transaction ---

namespace {
Step defStep(Op op, ParameterDef def) {
    Step step;
    step.op = op;
    step.def = std::move(def);
    return step;
}
Step nameStep(Op op, std::string name) {
    Step step;
    step.op = op;
    step.def.name = std::move(name);
    return step;
}
} // namespace

Transaction& Transaction::define(ParameterDef def) {
    steps_.push_back(defStep(Op::Define, std::move(def)));
    return *this;
}
Transaction& Transaction::redefine(ParameterDef def) {
    steps_.push_back(defStep(Op::Redefine, std::move(def)));
    return *this;
}
Transaction& Transaction::defineOrReplace(ParameterDef def) {
    steps_.push_back(defStep(Op::DefineOrReplace, std::move(def)));
    return *this;
}
Transaction& Transaction::setValue(std::string name, double value) {
    Step step = nameStep(Op::SetValue, std::move(name));
    step.def.value = value;
    steps_.push_back(std::move(step));
    return *this;
}
Transaction& Transaction::setExpression(std::string name, std::string expression) {
    Step step = nameStep(Op::SetExpression, std::move(name));
    step.def.expression = std::move(expression);
    steps_.push_back(std::move(step));
    return *this;
}
Transaction& Transaction::setBounds(std::string name, std::optional<double> lower, std::optional<double> upper) {
    Step step = nameStep(Op::SetBounds, std::move(name));
    step.def.lowerBound = lower;
    step.def.upperBound = upper;
    steps_.push_back(std::move(step));
    return *this;
}
Transaction& Transaction::remove(std::string name) {
    steps_.push_back(nameStep(Op::Remove, std::move(name)));
    return *this;
}
Transaction& Transaction::rename(std::string oldName, std::string newName) {
    Step step = nameStep(Op::Rename, std::move(oldName));
    step.newName = std::move(newName);
    steps_.push_back(std::move(step));
    return *this;
}
Transaction& Transaction::addConstraint(ConstraintDef constraint) {
    Step step;
    step.op = Op::AddConstraint;
    step.constraint = std::move(constraint);
    steps_.push_back(std::move(step));
    return *this;
}
Transaction& Transaction::removeConstraint(std::string name) {
    Step step;
    step.op = Op::RemoveConstraint;
    step.constraint.name = std::move(name);
    steps_.push_back(std::move(step));
    return *this;
}

// --- ParameterGraph: lifetime ---

ParameterGraph::ParameterGraph() : state_(std::make_shared<GraphState>()) {}
ParameterGraph::~ParameterGraph() = default;
ParameterGraph::ParameterGraph(const ParameterGraph& other)
    : state_(other.state_), stats_(other.stats_), diagnostics_(other.diagnostics_) {
    if (!state_) state_ = std::make_shared<GraphState>();
}
ParameterGraph& ParameterGraph::operator=(const ParameterGraph& other) {
    if (this != &other) {
        state_ = other.state_ ? other.state_ : std::make_shared<GraphState>();
        stats_ = other.stats_;
        diagnostics_ = other.diagnostics_;
    }
    return *this;
}
ParameterGraph::ParameterGraph(ParameterGraph&& other) noexcept
    : state_(std::move(other.state_)), stats_(std::move(other.stats_)),
      diagnostics_(std::move(other.diagnostics_)), observers_(std::move(other.observers_)),
      nextObserverId_(other.nextObserverId_) {}
ParameterGraph& ParameterGraph::operator=(ParameterGraph&& other) noexcept {
    if (this != &other) {
        state_ = std::move(other.state_);
        stats_ = std::move(other.stats_);
        diagnostics_ = std::move(other.diagnostics_);
        observers_ = std::move(other.observers_);
        nextObserverId_ = other.nextObserverId_;
    }
    return *this;
}

void ParameterGraph::ensureUnique() {
    // A moved-from graph has no state; a state shared with a snapshot or a copy is cloned first.
    if (!state_) state_ = std::make_shared<GraphState>();
    else if (state_.use_count() > 1) state_ = std::make_shared<GraphState>(*state_);
}

// --- ParameterGraph: mutation ---

core::Result<ChangeSet> ParameterGraph::run(const std::vector<Step>& steps, bool recalculateEverything) {
    ensureUnique();
    stats_ = RecalcStats{};
    diagnostics_.clear();
    detail::Journal journal;
    core::Result<void> result;
    for (const Step& step : steps) {
        result = detail::applyStep(*state_, step, journal);
        if (!result) break;
    }
    if (result) result = detail::validateAndRecalculate(*state_, journal, recalculateEverything, stats_, diagnostics_);
    if (!result) {
        detail::rollback(*state_, journal);
        if (!core::hasErrors(diagnostics_)) {
            const char* code = "parametric.change.rejected";
            switch (result.error().code) {
            case core::ErrorCode::ParseError: code = "parametric.expression.parse_error"; break;
            case core::ErrorCode::NotFound: code = "parametric.name.not_found"; break;
            case core::ErrorCode::AlreadyExists: code = "parametric.name.duplicate"; break;
            default: break;
            }
            diagnostics_.push_back({core::Severity::Error, code, result.error().message, result.error().context});
        }
        return result.error();
    }
    ChangeSet changes = detail::buildChangeSet(*state_, journal);
    changes.warnings = diagnostics_; // only warnings can be present after a successful commit
    notify(changes);
    return changes;
}

core::Result<ChangeSet> ParameterGraph::apply(const Transaction& transaction) {
    return run(transaction.steps(), false);
}
core::Result<ChangeSet> ParameterGraph::define(ParameterDef def) { return apply(Transaction().define(std::move(def))); }
core::Result<ChangeSet> ParameterGraph::redefine(ParameterDef def) {
    return apply(Transaction().redefine(std::move(def)));
}
core::Result<ChangeSet> ParameterGraph::defineConstant(std::string name, double value, ParameterKind kind) {
    return define(ParameterDef::constant(std::move(name), value, kind));
}
core::Result<ChangeSet> ParameterGraph::defineExpression(std::string name, std::string expression,
                                                         ParameterKind kind) {
    if (expression.empty()) {
        return core::makeError(core::ErrorCode::ParseError,
                               "Parameter '" + name + "': the expression is empty; use defineConstant for a constant.",
                               name);
    }
    return define(ParameterDef::formula(std::move(name), std::move(expression), kind));
}
core::Result<ChangeSet> ParameterGraph::setValue(std::string name, double value) {
    return apply(Transaction().setValue(std::move(name), value));
}
core::Result<ChangeSet> ParameterGraph::setExpression(std::string name, std::string expression) {
    return apply(Transaction().setExpression(std::move(name), std::move(expression)));
}
core::Result<ChangeSet> ParameterGraph::setBounds(std::string name, std::optional<double> lower,
                                                  std::optional<double> upper) {
    return apply(Transaction().setBounds(std::move(name), lower, upper));
}
core::Result<ChangeSet> ParameterGraph::remove(std::string name) {
    return apply(Transaction().remove(std::move(name)));
}
core::Result<ChangeSet> ParameterGraph::rename(std::string oldName, std::string newName) {
    return apply(Transaction().rename(std::move(oldName), std::move(newName)));
}
core::Result<ChangeSet> ParameterGraph::addConstraint(ConstraintDef constraint) {
    return apply(Transaction().addConstraint(std::move(constraint)));
}
core::Result<ChangeSet> ParameterGraph::removeConstraint(std::string name) {
    return apply(Transaction().removeConstraint(std::move(name)));
}
core::Result<ChangeSet> ParameterGraph::recalculateAll() { return run({}, true); }

core::Result<ParameterGraph> ParameterGraph::load(const std::vector<ParameterDef>& definitions,
                                                  const std::vector<ConstraintDef>& constraints) {
    Transaction tx;
    for (const auto& def : definitions) tx.define(def);
    for (const auto& c : constraints) tx.addConstraint(c);
    ParameterGraph graph;
    auto result = graph.apply(tx);
    if (!result) return result.error();
    return graph;
}

// --- ParameterGraph: queries ---

namespace {
const GraphState& emptyState() {
    static const GraphState state;
    return state;
}
} // namespace

#define FOS_STATE (state_ ? *state_ : emptyState())

bool ParameterGraph::contains(std::string_view name) const { return FOS_STATE.nodes.count(name) > 0; }
std::size_t ParameterGraph::size() const { return FOS_STATE.nodes.size(); }

core::Result<double> ParameterGraph::value(std::string_view name) const {
    const auto& nodes = FOS_STATE.nodes;
    const auto it = nodes.find(name);
    if (it == nodes.end()) {
        return core::makeError(core::ErrorCode::NotFound, "Parameter '" + std::string(name) + "' is not defined.",
                               std::string(name));
    }
    return it->second.def.value;
}

const ParameterDef* ParameterGraph::find(std::string_view name) const {
    const auto& nodes = FOS_STATE.nodes;
    const auto it = nodes.find(name);
    return it == nodes.end() ? nullptr : &it->second.def;
}

std::vector<std::string> ParameterGraph::names() const {
    std::vector<std::string> out;
    for (const auto& [name, node] : FOS_STATE.nodes) out.push_back(name);
    return out;
}

std::vector<ParameterDef> ParameterGraph::definitions() const {
    std::vector<ParameterDef> out;
    for (const auto& [name, node] : FOS_STATE.nodes) out.push_back(node.def);
    return out;
}

std::vector<ConstraintDef> ParameterGraph::constraints() const {
    std::vector<ConstraintDef> out;
    for (const auto& [name, c] : FOS_STATE.constraints) out.push_back(c.def);
    return out;
}

std::vector<std::string> ParameterGraph::dependencies(std::string_view name) const {
    const auto& nodes = FOS_STATE.nodes;
    const auto it = nodes.find(name);
    return it == nodes.end() ? std::vector<std::string>() : it->second.deps;
}

std::vector<std::string> ParameterGraph::dependents(std::string_view name) const {
    const auto& map = FOS_STATE.dependents;
    const auto it = map.find(name);
    if (it == map.end()) return {};
    return {it->second.begin(), it->second.end()};
}

std::vector<std::string> ParameterGraph::transitiveDependents(std::string_view name) const {
    std::vector<std::string> out;
    for (const std::string_view n : detail::orderFrom(FOS_STATE, {name})) {
        if (n != name) out.emplace_back(n);
    }
    return out;
}

std::vector<std::string> ParameterGraph::evaluationOrder() const {
    const GraphState& state = FOS_STATE;
    std::vector<std::string_view> roots;
    for (const auto& [name, node] : state.nodes) roots.push_back(name);
    std::vector<std::string> out;
    for (const std::string_view n : detail::orderFrom(state, roots)) out.emplace_back(n);
    return out;
}

core::Result<double> ParameterGraph::evaluate(std::string_view expression) const {
    auto parsed = parseExpression(expression);
    if (!parsed) return parsed.error();
    const GraphState& state = FOS_STATE;
    auto result = parametric::evaluate(*parsed.value(), [&state](const std::string& name) -> std::optional<double> {
        const auto it = state.nodes.find(name);
        if (it == state.nodes.end()) return std::nullopt;
        return it->second.def.value;
    });
    if (!result) {
        return core::makeError(result.error().code,
                               "Expression '" + std::string(expression) + "' cannot be evaluated: " +
                                   result.error().message + ".",
                               result.error().context);
    }
    return result;
}

core::Diagnostics ParameterGraph::checkConstraints() const {
    core::Diagnostics out;
    (void)detail::checkConstraints(FOS_STATE, nullptr, out, nullptr);
    return out;
}

// --- ParameterGraph: observers and snapshots ---

std::size_t ParameterGraph::addObserver(Observer observer) {
    observers_.emplace_back(nextObserverId_, std::move(observer));
    return nextObserverId_++;
}

void ParameterGraph::removeObserver(std::size_t id) {
    observers_.erase(std::remove_if(observers_.begin(), observers_.end(),
                                    [id](const auto& entry) { return entry.first == id; }),
                     observers_.end());
}

void ParameterGraph::notify(const ChangeSet& changes) {
    const auto observers = observers_; // observers may add/remove observers while being called
    for (const auto& [id, observer] : observers)
        if (observer) observer(changes);
}

Snapshot ParameterGraph::snapshot() const {
    Snapshot s;
    s.state_ = state_ ? state_ : std::make_shared<GraphState>();
    return s;
}

ChangeSet ParameterGraph::restore(const Snapshot& snapshot) {
    ChangeSet changes;
    if (!snapshot.valid()) return changes;
    const GraphState& before = FOS_STATE;
    const GraphState& after = *snapshot.state_;
    if (&before != &after) {
        for (const auto& [name, node] : after.nodes) {
            const auto old = before.nodes.find(name);
            if (old == before.nodes.end()) {
                changes.added.push_back(name);
                changes.changed.push_back(name);
            } else if (old->second.def.value != node.def.value) {
                changes.changed.push_back(name);
            }
        }
        for (const auto& [name, node] : before.nodes) {
            if (!after.nodes.count(name)) changes.removed.push_back(name);
        }
    }
    // Shared copy-on-write: ensureUnique() clones before the next mutation, so the snapshot stays intact.
    state_ = std::const_pointer_cast<GraphState>(snapshot.state_);
    stats_ = RecalcStats{};
    diagnostics_.clear();
    notify(changes);
    return changes;
}

#undef FOS_STATE

} // namespace fos::parametric
