// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Inspector view-model for parameters: grouping by kind, display formatting and turning
/// the text typed by the user into a project command. Values come from the parameter graph;
/// nothing is computed here.
#include "fos/core/result.hpp"
#include "fos/project/commands.hpp"

#include <QString>

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fos::app {

class Session;

struct ParameterRow {
    std::string name;
    parametric::ParameterKind kind = parametric::ParameterKind::Number;
    double value = 0;            ///< Current value from the graph (mm / degrees / number).
    std::string expression;      ///< Non-empty when the parameter is an expression.
    std::string description;
    bool standard = false;       ///< A furniture standard parameter (cabinets only).
    bool derived = false;        ///< Standard derived parameter (formula maintained by options).
    bool pinned = false;         ///< Overridden derived / material-synchronised parameter.
    std::optional<double> lowerBound, upperBound;
    bool integer = false;
};

struct ParameterGroup {
    parametric::ParameterKind kind = parametric::ParameterKind::Number;
    std::vector<ParameterRow> rows;
};

/// Rows of a cabinet grouped by kind, groups in the order Length, Count, Angle, Ratio,
/// Boolean, Number (empty groups omitted). Inside a group: standard inputs in
/// furniture::standardParameters() order, then derived standard parameters in that order,
/// then custom parameters by name.
std::vector<ParameterGroup> parameterGroups(const furniture::CabinetDefinition& cabinet);
/// Rows of a free panel (no standard/derived distinction; graph name order inside groups).
std::vector<ParameterGroup> parameterGroups(const furniture::PanelDefinition& panel);

/// Translated group caption ("Lengths", "Counts", ...).
QString kindTitle(parametric::ParameterKind kind);
/// Unit shown after the value: "mm" for Length, "°" for Angle, empty otherwise.
QString unitSuffix(parametric::ParameterKind kind);
/// Text for the editor: the expression when there is one, otherwise the value with up to
/// 3 decimals ("900", "0.5"); Boolean shows 0 / 1.
QString editText(const ParameterRow& row);
/// Value as displayed next to an expression: "864 mm".
QString valueText(const ParameterRow& row);

/// Classified user input.
struct ParameterInput {
    bool isExpression = false;
    double value = 0;       ///< When !isExpression.
    std::string expression; ///< When isExpression (trimmed).
};

/// Plain decimal numbers (locale-independent; one decimal comma is accepted as point; an
/// optional trailing "mm" is ignored) become values; anything else is passed on as an
/// expression for the parametric engine to judge. Empty input -> InvalidArgument.
core::Result<ParameterInput> parseParameterInput(const QString& text);

/// commands::setParameter / setParameterExpression for the input.
project::Command parameterCommand(const std::string& itemId, const std::string& name, const ParameterInput& input);

/// Parses and executes. On any error the project is unchanged and the error carries the
/// message of the engine (parametric / furniture / project) unchanged.
core::Result<void> applyParameterInput(Session& session, const std::string& itemId, const std::string& name,
                                       const QString& text);

} // namespace fos::app
