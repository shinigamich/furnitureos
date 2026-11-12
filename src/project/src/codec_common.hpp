// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Private helpers shared by the project codecs (not installed).
#include "fos/formats/codec.hpp"
#include "fos/formats/enums.hpp"
#include "fos/furniture/enums.hpp"
#include "fos/project/project.hpp"
#include "json_reader.hpp"

#include <initializer_list>

namespace fos::project::detail {

/// Parser of a furniture enumeration from its stable toString() text.
template <class E> auto furnitureEnum() {
    return [](std::string_view s) { return furniture::fromString<E>(s); };
}

/// "\"A\", \"B\"" for error messages.
template <class E> std::string furnitureTokens(std::initializer_list<E> values) {
    std::string out;
    for (const E v : values) out += (out.empty() ? "\"" : ", \"") + std::string(furniture::toString(v)) + "\"";
    return out;
}

/// Domain enumeration via the stable file tokens of the formats module.
template <class E> auto formatsEnum() {
    return [](std::string_view s) { return formats::enumFromToken<E>(s); };
}

/// Writes "parameters" (definitions sorted by name) and "constraints" (sorted by name).
void writeGraph(Json& object, const parametric::ParameterGraph& graph);
/// Reads "parameters" (required) and "constraints" and rebuilds the graph with load().
void readGraph(Reader& reader, parametric::ParameterGraph& graph);
/// {"l1": id|null, "l2": ..., "w1": ..., "w2": ...}
Json writeEdges(const std::array<std::optional<domain::EdgeBandId>, 4>& edges);
core::Result<std::array<std::optional<domain::EdgeBandId>, 4>> readEdges(const Json& json, std::string_view path);
/// Reads every key of a cabinet object except "type".
void readCabinetFields(Reader& reader, furniture::CabinetDefinition& cabinet);

} // namespace fos::project::detail
