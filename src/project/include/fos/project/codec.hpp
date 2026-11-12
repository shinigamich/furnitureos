// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// JSON codecs of the project payload (schema: src/project/README.md, public format
/// documentation). Encoding writes every known key in a fixed order, so encode(decode(x))
/// reproduces x exactly. Decoding is strict: wrong types, out-of-range values, unknown
/// enumeration tokens and unknown keys of closed objects are ParseError errors whose message
/// starts with a JSON-pointer path ("/payload/items/0/interior/children/1/shelfCount: ...");
/// Error::context holds the path. Unknown keys of the extensible objects (payload root,
/// "project", "settings", every item) are kept in their `extra` member and written back.
#include "fos/core/result.hpp"
#include "fos/project/project.hpp"

#include <string_view>

namespace fos::project {

formats::Json toJson(const parametric::ParameterDef& parameter);
formats::Json toJson(const parametric::ConstraintDef& constraint);
formats::Json toJson(const furniture::Section& section);
formats::Json toJson(const furniture::CabinetDefinition& cabinet); ///< Without the "type" tag.
formats::Json toJson(const drawings::DrawingEdits& edits);
formats::Json toJson(const CuttingSettings& settings);
formats::Json toJson(const Item& item);                            ///< With "type" and preserved keys.

core::Result<parametric::ParameterDef> parameterFromJson(const formats::Json& json, std::string_view path);
core::Result<parametric::ConstraintDef> constraintFromJson(const formats::Json& json, std::string_view path);
core::Result<furniture::Section> sectionFromJson(const formats::Json& json, std::string_view path);
/// Decodes a cabinet object (keys of toJson(CabinetDefinition); an optional "type" key is
/// accepted and must be "cabinet"). The parameter graph is rebuilt with ParameterGraph::load
/// (errors of the graph are reported at ".../parameters").
core::Result<furniture::CabinetDefinition> cabinetFromJson(const formats::Json& json, std::string_view path);
core::Result<drawings::DrawingEdits> drawingEditsFromJson(const formats::Json& json, std::string_view path);
core::Result<CuttingSettings> cuttingSettingsFromJson(const formats::Json& json, std::string_view path);
core::Result<Item> itemFromJson(const formats::Json& json, std::string_view path);

/// The whole payload of a `.fosp` file.
formats::Json projectToJson(const Project& project);
/// Decodes a payload; `path` prefixes messages (the file loader passes "/payload").
/// Also enforces validateStructure() (unique ids), reported with paths.
core::Result<Project> projectFromJson(const formats::Json& payload, std::string_view path = "");

} // namespace fos::project
