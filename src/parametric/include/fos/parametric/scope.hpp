// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Name-prefix scoping for nested assemblies: instantiate one set of parameter definitions under
/// different prefixes ("cab1.width", "cab2.width").
#include <string>
#include <string_view>
#include <vector>

#include "fos/core/result.hpp"
#include "fos/parametric/parameter_graph.hpp"

namespace fos::parametric {

/// First name segment that refers to the enclosing scope inside a template expression:
/// `outer.width` means "the parameter `width` of the scope this template is instantiated in".
inline constexpr std::string_view kOuterScope = "outer";

/// qualify("cab1", "width") == "cab1.width"; an empty prefix returns `name` unchanged.
std::string qualify(std::string_view prefix, std::string_view name);

/// A reusable set of definitions written with local (unprefixed) names.
struct ParameterTemplate {
    std::vector<ParameterDef> parameters;
    std::vector<ConstraintDef> constraints;
};

/// Returns a copy of the template in which
///  * every parameter name and constraint name is prefixed: `width` -> `<prefix>.width`;
///  * identifiers in expressions (and `{name}` placeholders in constraint messages) that name a
///    parameter of the template are prefixed the same way;
///  * identifiers starting with `outer.` lose that segment and are not prefixed (`outer.width` ->
///    `width`), i.e. they refer to the enclosing scope; `outer.outer.x` survives one more level;
///  * all other identifiers are left unchanged (absolute references).
/// Expressions are returned in canonical toString form. Instantiating the result again under
/// another prefix nests scopes (`cab1.shelf.length`). Errors: invalid prefix, unparsable expression.
core::Result<ParameterTemplate> instantiate(const ParameterTemplate& tmpl, std::string_view prefix);

/// Appends `define` / `addConstraint` steps for everything in the template to a transaction.
void appendTo(Transaction& transaction, const ParameterTemplate& tmpl);

/// Concatenates templates (e.g. a cabinet's own parameters plus instantiated sub-templates).
ParameterTemplate merge(ParameterTemplate a, const ParameterTemplate& b);

} // namespace fos::parametric
