// SPDX-License-Identifier: AGPL-3.0-only
// `.fostools` codec and file I/O (value rules: tool_table_check.cpp).
#include "fos/formats/tool_table.hpp"

#include "codec_reader.hpp"

#include <cmath>
#include <limits>

namespace fos::formats {

using detail::describe;
using detail::NumberRule;
using detail::ObjectReader;

const char* toolTypeToken(cam::ToolKind kind) noexcept {
    switch (kind) {
    case cam::ToolKind::Drill: return "drill";
    case cam::ToolKind::EndMill: return "flat_end_mill";
    }
    return "unknown";
}

std::optional<cam::ToolKind> toolTypeFromToken(std::string_view token) noexcept {
    if (token == "drill") return cam::ToolKind::Drill;
    if (token == "flat_end_mill") return cam::ToolKind::EndMill;
    return std::nullopt;
}

const FormatSpec& toolsFormatSpec() {
    static const FormatSpec spec{std::string(kToolsFormatKind), kToolsFormatVersion, {}};
    return spec;
}

namespace {

Json toolToJson(const ToolEntry& t) {
    Json j = Json::object();
    j["id"] = t.id;
    j["number"] = t.number;
    if (!t.name.empty()) j["name"] = t.name;
    j["type"] = toolTypeToken(t.type);
    j["diameter"] = t.diameter;
    j["fluteLength"] = t.fluteLength;
    if (t.overallLength) j["overallLength"] = *t.overallLength;
    if (t.maxDepthPerPass) j["maxDepthPerPass"] = *t.maxDepthPerPass;
    if (t.type == cam::ToolKind::EndMill) j["centerCutting"] = t.centerCutting;
    if (t.spindleRpm) j["spindleRpm"] = *t.spindleRpm;
    if (t.feedRate) j["feedRate"] = *t.feedRate;
    if (t.plungeRate) j["plungeRate"] = *t.plungeRate;
    if (!t.notes.empty()) j["notes"] = t.notes;
    return j;
}

void optionalNumber(ObjectReader& r, std::string_view key, std::optional<double>& out) {
    const Json* value = r.find(key);
    if (value == nullptr) return;
    auto number = detail::readNumber(*value, r.childPath(key), NumberRule::Any);
    if (!number) return r.fail(number.error());
    out = number.value();
}

core::Result<ToolEntry> toolFromJson(const Json& json, std::string_view path) {
    ObjectReader r(json, path, "a tool");
    ToolEntry t;
    r.text("id", t.id, true, true);
    if (const Json* n = r.find("number", true)) {
        // Range first: converting an out-of-range double to an integer is undefined.
        const double v = n->is_number() ? n->get<double>() : 0.5;
        const bool integral = n->is_number() && std::isfinite(v) && std::floor(v) == v &&
                              v >= static_cast<double>(std::numeric_limits<int>::min()) &&
                              v <= static_cast<double>(std::numeric_limits<int>::max());
        if (!integral)
            r.failAt("number", "expected an integer tool number, got " + describe(*n));
        else
            t.number = static_cast<int>(v);
    }
    r.text("name", t.name);
    if (const Json* type = r.find("type", true)) {
        std::optional<cam::ToolKind> kind;
        if (type->is_string()) kind = toolTypeFromToken(type->get<std::string>());
        if (kind)
            t.type = *kind;
        else
            r.failAt("type", "expected one of \"drill\", \"flat_end_mill\", got " + describe(*type) +
                                 " (other tool shapes such as ball-nose or compression cutters are not modelled by "
                                 "the CAM module)");
    }
    r.number("diameter", t.diameter, NumberRule::Any, true);
    r.number("fluteLength", t.fluteLength, NumberRule::Any, true);
    optionalNumber(r, "overallLength", t.overallLength);
    optionalNumber(r, "maxDepthPerPass", t.maxDepthPerPass);
    r.boolean("centerCutting", t.centerCutting);
    optionalNumber(r, "spindleRpm", t.spindleRpm);
    optionalNumber(r, "feedRate", t.feedRate);
    optionalNumber(r, "plungeRate", t.plungeRate);
    r.text("notes", t.notes);
    r.rejectUnknown();
    if (!r.ok()) return r.error();
    return t;
}

} // namespace

Json toolTableToJson(const ToolTable& table) {
    Json payload = Json::object();
    Json tools = Json::array();
    for (const auto& t : table.tools) tools.push_back(toolToJson(t));
    payload["tools"] = std::move(tools);
    for (const auto& [key, value] : table.extra.items())
        if (key != "tools") payload[key] = value;
    return payload;
}

core::Result<ToolTable> toolTableFromJson(const Json& payload, std::string_view path) {
    if (!payload.is_object()) return detail::schemaError(path, "expected a tool table object, got " + describe(payload));
    ToolTable table;
    const auto it = payload.find("tools");
    if (it == payload.end()) return detail::schemaError(detail::joinPath(path, "tools"), "required key is missing");
    if (!it->is_array())
        return detail::schemaError(detail::joinPath(path, "tools"), "expected an array, got " + describe(*it));
    const std::string base = detail::joinPath(path, "tools");
    for (std::size_t i = 0; i < it->size(); ++i) {
        auto tool = toolFromJson((*it)[i], detail::joinPath(base, i));
        if (!tool) return tool.error();
        table.tools.push_back(std::move(tool.value()));
    }
    for (const auto& [key, value] : payload.items())
        if (key != "tools") table.extra[key] = value;
    return table;
}

core::Result<SaveReport> saveToolTable(const std::filesystem::path& path, const ToolTable& table, const AppInfo& app,
                                       const SaveOptions& options) {
    for (const auto& d : validateToolTable(table))
        if (d.severity == core::Severity::Error)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "The tool table was not written to '" + path.filename().string() + "': " + d.message,
                                   d.entityId);
    Document document;
    document.format = std::string(kToolsFormatKind);
    document.formatVersion = kToolsFormatVersion;
    document.app = app;
    document.payload = toolTableToJson(table);
    return saveDocument(path, std::move(document), options);
}

core::Result<LoadedToolTable> parseToolTableText(std::string_view text, std::string_view sourceName,
                                                 const LoadOptions& options) {
    const std::string name(sourceName);
    if (text.size() > options.maxFileBytes)
        return core::makeError(core::ErrorCode::ConstraintViolation,
                               "Tool table '" + name + "' is " + std::to_string(text.size()) +
                                   " bytes; the limit is " + std::to_string(options.maxFileBytes) +
                                   " bytes. It was not parsed.",
                               name);
    auto loaded = parseDocument(text, toolsFormatSpec(), options, name);
    if (!loaded) return loaded.error();
    auto table = toolTableFromJson(loaded.value().document.payload, "/payload");
    if (!table) {
        core::Error error = table.error();
        error.message = "'" + name + "' is not a valid tool table: " + error.message;
        return error;
    }
    LoadedToolTable result;
    result.table = std::move(table.value());
    result.diagnostics = std::move(loaded.value().diagnostics);
    for (auto& d : validateToolTable(result.table)) result.diagnostics.push_back(std::move(d));
    return result;
}

core::Result<LoadedToolTable> loadToolTable(const std::filesystem::path& path, const LoadOptions& options) {
    auto text = readFile(path, ReadLimits{options.maxFileBytes});
    if (!text) return text.error();
    const auto u8 = path.filename().u8string();
    return parseToolTableText(text.value(), std::string(u8.begin(), u8.end()), options);
}

} // namespace fos::formats
