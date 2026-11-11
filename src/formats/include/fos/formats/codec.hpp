// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/domain/model.hpp"
#include "fos/formats/json.hpp"

#include <string_view>

/// JSON codecs of the domain types. The schema of every type is documented in
/// src/formats/README.md ("Codec schemas") and is public file-format documentation.
///
/// Encoding (`toJson`) always writes every known key, lengths in mm, enumerations as
/// the stable tokens of enums.hpp.
///
/// Decoding (`...FromJson`) is strict: wrong types, out-of-range numbers, unknown
/// enumeration tokens, missing required keys and duplicate ids are errors
/// (ErrorCode::ParseError). The message starts with a JSON-pointer-like location, e.g.
/// "/assemblies/0/parts/3/length: expected a positive number, got -5"; Error::context
/// holds the location alone. `path` is the location of the decoded value inside its
/// document and only prefixes messages.
///
/// UNKNOWN KEYS are never dropped silently:
///  * Material, EdgeBand, HardwareDefinition and Part keep an unknown key `k` in their
///    `custom` map as `custom["json:k"] = <compact JSON text of the value>`, and toJson
///    writes it back as the top-level key `k`.
///  * Color, Transform, HardwareInstance, Operation, Assembly and Model have no such
///    map: an unknown key is an error naming the key and the allowed keys.
namespace fos::formats {

/// Prefix of `custom` entries that carry preserved unknown JSON keys. Reserved: do not
/// use it for user-defined properties.
inline constexpr std::string_view kUnknownKeyPrefix = "json:";

Json toJson(const domain::Color& color);              ///< "#RRGGBB" (upper-case hex).
Json toJson(const geometry::Transform& placement);    ///< {"origin","xAxis","yAxis","zAxis"} arrays, mm.
Json toJson(const domain::Material& material);
Json toJson(const domain::EdgeBand& edgeBand);
Json toJson(const domain::HardwareDefinition& definition);
Json toJson(const domain::HardwareInstance& instance);
Json toJson(const domain::Operation& operation);       ///< Tagged with "type": "hole" | "groove" | "pocket".
Json toJson(const domain::Part& part);
Json toJson(const domain::Assembly& assembly);
Json toJson(const domain::Model& model);

core::Result<domain::Color> colorFromJson(const Json& json, std::string_view path = "");
/// Fails unless the axes form a proper rotation (geometry::Transform::isRigid, eps 1e-9).
core::Result<geometry::Transform> transformFromJson(const Json& json, std::string_view path = "");
core::Result<domain::Material> materialFromJson(const Json& json, std::string_view path = "");
core::Result<domain::EdgeBand> edgeBandFromJson(const Json& json, std::string_view path = "");
core::Result<domain::HardwareDefinition> hardwareDefinitionFromJson(const Json& json, std::string_view path = "");
core::Result<domain::HardwareInstance> hardwareInstanceFromJson(const Json& json, std::string_view path = "");
core::Result<domain::Operation> operationFromJson(const Json& json, std::string_view path = "");
core::Result<domain::Part> partFromJson(const Json& json, std::string_view path = "");
core::Result<domain::Assembly> assemblyFromJson(const Json& json, std::string_view path = "");
core::Result<domain::Model> modelFromJson(const Json& json, std::string_view path = "");

/// toJson(model) followed by a decode of the result. Fails (with the decoder's message)
/// when the model holds values the format cannot represent or would reject on load —
/// NaN/infinite numbers, non-positive sizes, non-rigid placements, duplicate ids, ... —
/// so that a file that cannot be read back is never written.
core::Result<Json> modelToJsonChecked(const domain::Model& model);

} // namespace fos::formats
