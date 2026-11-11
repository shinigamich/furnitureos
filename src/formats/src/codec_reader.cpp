// SPDX-License-Identifier: AGPL-3.0-only
#include "codec_reader.hpp"

#include "fos/formats/codec.hpp"

#include <algorithm>
#include <cmath>

namespace fos::formats::detail {

std::string describe(const Json& value) {
    switch (value.type()) {
    case Json::value_t::null: return "null";
    case Json::value_t::object: return "an object";
    case Json::value_t::array: return "an array";
    case Json::value_t::boolean: return value.get<bool>() ? "true" : "false";
    case Json::value_t::string: {
        std::string text = value.get<std::string>();
        if (text.size() > 40) {
            // Cut on a UTF-8 character boundary.
            std::size_t cut = 40;
            while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
            text = text.substr(0, cut) + "...";
        }
        return "\"" + text + "\"";
    }
    case Json::value_t::number_integer:
    case Json::value_t::number_unsigned:
    case Json::value_t::number_float: return value.dump();
    default: return "an unsupported value";
    }
}

core::Error schemaError(std::string_view path, const std::string& message) {
    const std::string where = path.empty() ? std::string("/") : std::string(path);
    return core::makeError(core::ErrorCode::ParseError, where + ": " + message, where);
}

std::string joinPath(std::string_view path, std::string_view key) {
    std::string out(path);
    out += '/';
    out += key;
    return out;
}

std::string joinPath(std::string_view path, std::size_t index) { return joinPath(path, std::to_string(index)); }

core::Result<double> readNumber(const Json& value, std::string_view path, NumberRule rule) {
    const char* expected = rule == NumberRule::Positive      ? "expected a positive number"
                           : rule == NumberRule::NonNegative ? "expected a non-negative number"
                                                             : "expected a number";
    if (!value.is_number()) return schemaError(path, std::string(expected) + ", got " + describe(value));
    const double number = value.get<double>();
    const bool valid = std::isfinite(number) && (rule == NumberRule::Any ||
                                                 (rule == NumberRule::Positive && number > 0.0) ||
                                                 (rule == NumberRule::NonNegative && number >= 0.0));
    if (!valid) return schemaError(path, std::string(expected) + ", got " + describe(value));
    return number;
}

core::Result<std::string> readString(const Json& value, std::string_view path, bool nonEmpty) {
    if (!value.is_string()) return schemaError(path, "expected a string, got " + describe(value));
    std::string text = value.get<std::string>();
    if (nonEmpty && text.empty()) return schemaError(path, "expected a non-empty string, got \"\"");
    return text;
}

namespace {
core::Result<std::vector<double>> readCoordinates(const Json& value, std::string_view path, std::size_t count,
                                                  const char* shape) {
    if (!value.is_array() || value.size() != count)
        return schemaError(path, std::string("expected ") + shape + ", got " +
                                     (value.is_array() ? "an array of " + std::to_string(value.size()) + " elements"
                                                       : describe(value)));
    std::vector<double> out;
    for (std::size_t i = 0; i < count; ++i) {
        auto number = readNumber(value[i], joinPath(path, i), NumberRule::Any);
        if (!number) return number.error();
        out.push_back(number.value());
    }
    return out;
}
} // namespace

core::Result<geometry::Vec2> readVec2(const Json& value, std::string_view path) {
    auto c = readCoordinates(value, path, 2, "an array of 2 numbers [u, v]");
    if (!c) return c.error();
    return geometry::Vec2{c.value()[0], c.value()[1]};
}

core::Result<geometry::Vec3> readVec3(const Json& value, std::string_view path) {
    auto c = readCoordinates(value, path, 3, "an array of 3 numbers [x, y, z]");
    if (!c) return c.error();
    return geometry::Vec3{c.value()[0], c.value()[1], c.value()[2]};
}

Json writeVec2(geometry::Vec2 v) { return Json::array({v.x, v.y}); }
Json writeVec3(geometry::Vec3 v) { return Json::array({v.x, v.y, v.z}); }

void writeCustom(Json& object, const std::map<std::string, std::string>& custom) {
    Json plain = Json::object();
    std::vector<std::pair<std::string, Json>> restored;
    for (const auto& [key, text] : custom) {
        if (key.size() > kUnknownKeyPrefix.size() && key.compare(0, kUnknownKeyPrefix.size(), kUnknownKeyPrefix) == 0) {
            const std::string name = key.substr(kUnknownKeyPrefix.size());
            Json parsed = Json::parse(text, nullptr, false);
            // An entry that cannot go back to the top level (not JSON, or the name is a
            // known key) stays an ordinary custom property so that nothing is lost.
            if (!parsed.is_discarded() && name != "custom" && !object.contains(name)) {
                restored.emplace_back(name, std::move(parsed));
                continue;
            }
        }
        plain[key] = text;
    }
    if (!plain.empty()) object["custom"] = std::move(plain);
    for (auto& [name, value] : restored) object[name] = std::move(value);
}

// ---------------------------------------------------------------- ObjectReader

ObjectReader::ObjectReader(const Json& json, std::string_view path, std::string_view what)
    : json_(&json), path_(path) {
    if (!json.is_object())
        fail(schemaError(path, "expected " + std::string(what) + " (an object), got " + describe(json)));
}

const Json* ObjectReader::find(std::string_view key, bool required) {
    if (!ok()) return nullptr;
    consumed_.emplace_back(key);
    const auto it = json_->find(std::string(key));
    if (it == json_->end()) {
        if (required) failAt(key, "required key is missing");
        return nullptr;
    }
    return &*it;
}

void ObjectReader::number(std::string_view key, double& out, NumberRule rule, bool required) {
    const Json* value = find(key, required);
    if (value == nullptr) return;
    auto number = readNumber(*value, childPath(key), rule);
    if (!number) return fail(number.error());
    out = number.value();
}

void ObjectReader::text(std::string_view key, std::string& out, bool required, bool nonEmpty) {
    const Json* value = find(key, required);
    if (value == nullptr) return;
    auto text = readString(*value, childPath(key), nonEmpty);
    if (!text) return fail(text.error());
    out = std::move(text.value());
}

void ObjectReader::boolean(std::string_view key, bool& out) {
    const Json* value = find(key);
    if (value == nullptr) return;
    if (!value->is_boolean()) return failAt(key, "expected true or false, got " + describe(*value));
    out = value->get<bool>();
}

void ObjectReader::customAndUnknown(std::map<std::string, std::string>& custom) {
    if (const Json* value = find("custom")) {
        if (!value->is_object()) return failAt("custom", "expected an object of strings, got " + describe(*value));
        const std::string base = childPath("custom");
        for (const auto& [key, entry] : value->items()) {
            if (!entry.is_string())
                return fail(schemaError(joinPath(base, key), "expected a string, got " + describe(entry)));
            custom[key] = entry.get<std::string>();
        }
    }
    if (!ok()) return;
    for (const auto& [key, value] : json_->items()) {
        if (std::find(consumed_.begin(), consumed_.end(), key) != consumed_.end()) continue;
        if (key.empty()) return failAt(key, "an empty key name is not supported");
        const std::string stashKey = std::string(kUnknownKeyPrefix) + key;
        if (custom.count(stashKey) != 0)
            return fail(schemaError(childPath(key), "unknown key collides with the custom property \"" + stashKey +
                                                        "\"; remove one of them"));
        custom[stashKey] = value.dump(-1, ' ', false, Json::error_handler_t::replace);
    }
}

void ObjectReader::rejectUnknown() {
    if (!ok()) return;
    for (const auto& [key, value] : json_->items()) {
        if (std::find(consumed_.begin(), consumed_.end(), key) != consumed_.end()) continue;
        std::string allowed;
        for (const std::string& name : consumed_) {
            if (!allowed.empty()) allowed += ", ";
            allowed += "\"" + name + "\"";
        }
        return failAt(key, "unknown key (allowed keys here: " + allowed + ")");
    }
}

} // namespace fos::formats::detail
