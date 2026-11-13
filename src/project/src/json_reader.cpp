// SPDX-License-Identifier: AGPL-3.0-only
#include "json_reader.hpp"

#include <algorithm>
#include <cmath>

namespace fos::project::detail {

std::string describe(const Json& value) {
    if (value.is_null()) return "null";
    if (value.is_object()) return "an object";
    if (value.is_array()) return "an array";
    std::string text = value.dump();
    if (text.size() > 40) text = text.substr(0, 37) + "...";
    return text;
}

core::Error schemaError(std::string_view path, const std::string& message) {
    const std::string p = path.empty() ? "/" : std::string(path);
    return core::makeError(core::ErrorCode::ParseError, p + ": " + message, p);
}

std::string joinPath(std::string_view path, std::string_view key) {
    std::string out(path);
    out += '/';
    for (char c : key) {
        if (c == '~') out += "~0";
        else if (c == '/') out += "~1";
        else out += c;
    }
    return out;
}

std::string joinPath(std::string_view path, std::size_t index) { return std::string(path) + "/" + std::to_string(index); }

core::Result<double> readNumber(const Json& value, std::string_view path, Num rule) {
    const char* expected = rule == Num::Positive      ? "a positive number"
                           : rule == Num::NonNegative ? "a non-negative number"
                                                      : "a number";
    if (!value.is_number()) return schemaError(path, std::string("expected ") + expected + ", got " + describe(value));
    const double d = value.get<double>();
    if (!std::isfinite(d) || (rule == Num::Positive && !(d > 0)) || (rule == Num::NonNegative && !(d >= 0)))
        return schemaError(path, std::string("expected ") + expected + ", got " + describe(value));
    return d;
}

core::Result<geometry::Vec2> readVec2(const Json& value, std::string_view path) {
    if (!value.is_array() || value.size() != 2)
        return schemaError(path, "expected an array [u, v] of two numbers, got " + describe(value));
    auto u = readNumber(value[0], joinPath(path, 0), Num::Any);
    if (!u) return u.error();
    auto v = readNumber(value[1], joinPath(path, 1), Num::Any);
    if (!v) return v.error();
    return geometry::Vec2{u.value(), v.value()};
}

Json writeVec2(geometry::Vec2 v) { return Json::array({v.x, v.y}); }

Reader::Reader(const Json& json, std::string_view path, std::string_view what) : json_(&json), path_(path) {
    if (!json.is_object()) fail(schemaError(path, "expected " + std::string(what) + " (an object), got " + describe(json)));
}

const Json* Reader::find(std::string_view key, bool required) {
    known_.emplace_back(key);
    if (!ok()) return nullptr;
    const auto it = json_->find(std::string(key));
    if (it == json_->end()) {
        if (required) failAt(key, "required key is missing");
        return nullptr;
    }
    return &*it;
}

void Reader::text(std::string_view key, std::string& out, bool required, bool nonEmpty) {
    const Json* v = find(key, required);
    if (v == nullptr) return;
    if (!v->is_string() || (nonEmpty && v->get_ref<const std::string&>().empty()))
        return failAt(key, std::string("expected a ") + (nonEmpty ? "non-empty " : "") + "string, got " + describe(*v));
    out = v->get<std::string>();
}

void Reader::number(std::string_view key, double& out, Num rule, bool required) {
    const Json* v = find(key, required);
    if (v == nullptr) return;
    auto r = readNumber(*v, at(key), rule);
    if (!r) return fail(r.error());
    out = r.value();
}

void Reader::optionalNumber(std::string_view key, std::optional<double>& out, Num rule) {
    const Json* v = find(key);
    out.reset();
    if (v == nullptr || v->is_null()) return;
    auto r = readNumber(*v, at(key), rule);
    if (!r) return fail(r.error());
    out = r.value();
}

void Reader::optionalText(std::string_view key, std::optional<std::string>& out, bool nonEmpty) {
    const Json* v = find(key);
    out.reset();
    if (v == nullptr || v->is_null()) return;
    if (!v->is_string() || (nonEmpty && v->get_ref<const std::string&>().empty()))
        return failAt(key, std::string("expected null or a ") + (nonEmpty ? "non-empty " : "") + "string, got " +
                               describe(*v));
    out = v->get<std::string>();
}

void Reader::integer(std::string_view key, int& out, int minimum, bool required) {
    const Json* v = find(key, required);
    if (v == nullptr) return;
    if (v->is_number_integer()) {
        const auto i = v->get<long long>();
        if (i >= minimum && i <= 1000000000LL) {
            out = static_cast<int>(i);
            return;
        }
    }
    failAt(key, "expected an integer >= " + std::to_string(minimum) + ", got " + describe(*v));
}

void Reader::integerInRange(std::string_view key, int& out, int minimum, int maximum, bool required) {
    const Json* v = find(key, required);
    if (v == nullptr) return;
    if (v->is_number_integer()) {
        const auto i = v->get<long long>();
        if (i >= minimum && i <= maximum) {
            out = static_cast<int>(i);
            return;
        }
    }
    failAt(key, "expected an integer from " + std::to_string(minimum) + " to " + std::to_string(maximum) + ", got " +
                    describe(*v));
}

void Reader::limitArray(std::string_view key, std::size_t maxSize, const std::string& what) {
    const Json* v = find(key);
    if (v == nullptr || !v->is_array() || v->size() <= maxSize) return;
    failAt(key, "has " + std::to_string(v->size()) + " " + what + "; the limit is " + std::to_string(maxSize));
}

void Reader::boolean(std::string_view key, bool& out) {
    const Json* v = find(key);
    if (v == nullptr) return;
    if (!v->is_boolean()) return failAt(key, "expected true or false, got " + describe(*v));
    out = v->get<bool>();
}

void Reader::vec2(std::string_view key, geometry::Vec2& out, bool required) {
    const Json* v = find(key, required);
    if (v == nullptr) return;
    auto r = readVec2(*v, at(key));
    if (!r) return fail(r.error());
    out = r.value();
}

Json Reader::unknown() const {
    Json out = Json::object();
    if (!json_->is_object()) return out;
    for (auto it = json_->begin(); it != json_->end(); ++it)
        if (std::find(known_.begin(), known_.end(), it.key()) == known_.end()) out[it.key()] = it.value();
    return out;
}

void Reader::rejectUnknown() {
    if (!ok()) return;
    for (auto it = json_->begin(); it != json_->end(); ++it) {
        if (std::find(known_.begin(), known_.end(), it.key()) != known_.end()) continue;
        std::string allowed;
        for (const auto& k : known_) allowed += (allowed.empty() ? "\"" : ", \"") + k + "\"";
        return failAt(it.key(), "unknown key \"" + it.key() + "\" (allowed keys: " + allowed + ")");
    }
}

void appendExtra(Json& object, const Json& extra) {
    if (!extra.is_object()) return;
    for (auto it = extra.begin(); it != extra.end(); ++it)
        if (!object.contains(it.key())) object[it.key()] = it.value();
}

} // namespace fos::project::detail
