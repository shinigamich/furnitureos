// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/parameter_rows.hpp"

#include "fos/app/session.hpp"
#include "fos/core/units.hpp"
#include "fos/furniture/parameters.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <charconv>
#include <map>

namespace fos::app {

using parametric::ParameterKind;

namespace {

constexpr ParameterKind kGroupOrder[] = {ParameterKind::Length, ParameterKind::Count,   ParameterKind::Angle,
                                         ParameterKind::Ratio,  ParameterKind::Boolean, ParameterKind::Number};

ParameterRow makeRow(const parametric::ParameterGraph& graph, const parametric::ParameterDef& def) {
    ParameterRow row;
    row.name = def.name;
    row.kind = def.kind;
    row.value = graph.value(def.name).valueOr(0.0);
    row.expression = def.expression;
    row.description = def.description;
    row.lowerBound = def.lowerBound;
    row.upperBound = def.upperBound;
    row.integer = def.integer;
    return row;
}

std::vector<ParameterGroup> group(std::vector<ParameterRow> rows) {
    std::vector<ParameterGroup> groups;
    for (ParameterKind kind : kGroupOrder) {
        ParameterGroup g{kind, {}};
        for (auto& r : rows)
            if (r.kind == kind) g.rows.push_back(std::move(r));
        if (!g.rows.empty()) groups.push_back(std::move(g));
    }
    return groups;
}

} // namespace

std::vector<ParameterGroup> parameterGroups(const furniture::CabinetDefinition& cabinet) {
    // Ordering keys: standard inputs (0, index), derived standard (1, index), custom (2, name).
    std::map<std::string, std::size_t> standardIndex;
    const auto standard = furniture::standardParameters(cabinet.construction);
    for (std::size_t i = 0; i < standard.size(); ++i) standardIndex.emplace(standard[i].name, i);

    std::vector<ParameterRow> rows;
    for (const auto& def : cabinet.parameters.definitions()) {
        ParameterRow row = makeRow(cabinet.parameters, def);
        row.standard = furniture::isStandardParameter(def.name);
        row.derived = furniture::isDerivedParameter(def.name);
        row.pinned = cabinet.pinnedParameters.count(def.name) > 0;
        rows.push_back(std::move(row));
    }
    auto key = [&](const ParameterRow& r) {
        const auto it = standardIndex.find(r.name);
        const int tier = it == standardIndex.end() ? 2 : (r.derived ? 1 : 0);
        const std::size_t index = it == standardIndex.end() ? 0 : it->second;
        return std::tuple<int, std::size_t, std::string>(tier, index, r.name);
    };
    std::stable_sort(rows.begin(), rows.end(), [&](const ParameterRow& a, const ParameterRow& b) { return key(a) < key(b); });
    return group(std::move(rows));
}

std::vector<ParameterGroup> parameterGroups(const furniture::PanelDefinition& panel) {
    std::vector<ParameterRow> rows;
    for (const auto& def : panel.parameters.definitions()) rows.push_back(makeRow(panel.parameters, def));
    return group(std::move(rows));
}

QString kindTitle(ParameterKind kind) {
    switch (kind) {
    case ParameterKind::Length: return QCoreApplication::translate("fos::app::Parameters", "Lengths");
    case ParameterKind::Count: return QCoreApplication::translate("fos::app::Parameters", "Counts");
    case ParameterKind::Angle: return QCoreApplication::translate("fos::app::Parameters", "Angles");
    case ParameterKind::Ratio: return QCoreApplication::translate("fos::app::Parameters", "Ratios");
    case ParameterKind::Boolean: return QCoreApplication::translate("fos::app::Parameters", "Switches");
    case ParameterKind::Number: return QCoreApplication::translate("fos::app::Parameters", "Numbers");
    }
    return {};
}

QString unitSuffix(ParameterKind kind) {
    switch (kind) {
    case ParameterKind::Length: return QCoreApplication::translate("fos::app::Parameters", "mm");
    case ParameterKind::Angle: return QStringLiteral("°");
    default: return {};
    }
}

QString editText(const ParameterRow& row) {
    if (!row.expression.empty()) return QString::fromStdString(row.expression);
    return QString::fromStdString(core::formatLength(row.value, 3));
}

QString valueText(const ParameterRow& row) {
    const QString number = QString::fromStdString(core::formatLength(row.value, 3));
    const QString unit = unitSuffix(row.kind);
    return unit.isEmpty() ? number : (unit == QStringLiteral("°") ? number + unit : number + QLatin1Char(' ') + unit);
}

core::Result<ParameterInput> parseParameterInput(const QString& text) {
    QString t = text.trimmed();
    if (t.isEmpty())
        return core::makeError(core::ErrorCode::InvalidArgument,
                               QCoreApplication::translate("fos::app::Parameters", "Enter a number or an expression.")
                                   .toStdString());
    QString numeric = t;
    if (numeric.endsWith(QLatin1String("mm"), Qt::CaseInsensitive)) numeric = numeric.chopped(2).trimmed();
    if (numeric.count(QLatin1Char(',')) == 1 && !numeric.contains(QLatin1Char('.')))
        numeric.replace(QLatin1Char(','), QLatin1Char('.'));
    const std::string s = numeric.toStdString();
    double value = 0;
    const char* begin = s.data();
    const char* end = s.data() + s.size();
    if (!s.empty()) {
        const auto [ptr, ec] = std::from_chars(begin, end, value);
        if (ec == std::errc() && ptr == end) return ParameterInput{false, value, {}};
    }
    return ParameterInput{true, 0.0, t.toStdString()};
}

project::Command parameterCommand(const std::string& itemId, const std::string& name, const ParameterInput& input) {
    if (input.isExpression) return project::commands::setParameterExpression(itemId, name, input.expression);
    return project::commands::setParameter(itemId, name, input.value);
}

core::Result<void> applyParameterInput(Session& session, const std::string& itemId, const std::string& name,
                                       const QString& text) {
    auto input = parseParameterInput(text);
    if (!input) return input.error();
    return session.execute(parameterCommand(itemId, name, input.value()));
}

} // namespace fos::app
