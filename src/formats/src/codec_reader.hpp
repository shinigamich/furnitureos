// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Private helpers of the JSON codecs (not installed).
#include "fos/core/result.hpp"
#include "fos/formats/enums.hpp"
#include "fos/formats/json.hpp"
#include "fos/geometry/vec.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fos::formats::detail {

/// Short rendering of a JSON value for "got ..." messages.
std::string describe(const Json& value);

/// ParseError with message "<path>: <message>" and context = path ("/" for the root).
core::Error schemaError(std::string_view path, const std::string& message);

std::string joinPath(std::string_view path, std::string_view key);
std::string joinPath(std::string_view path, std::size_t index);

enum class NumberRule { Any, Positive, NonNegative };

core::Result<double> readNumber(const Json& value, std::string_view path, NumberRule rule);
core::Result<std::string> readString(const Json& value, std::string_view path, bool nonEmpty);
core::Result<geometry::Vec2> readVec2(const Json& value, std::string_view path);
core::Result<geometry::Vec3> readVec3(const Json& value, std::string_view path);
Json writeVec2(geometry::Vec2 v);
Json writeVec3(geometry::Vec3 v);

/// Writes the `custom` map of an entity into its JSON object: preserved unknown keys
/// ("json:<name>") go back to the top level, everything else into the "custom" object.
void writeCustom(Json& object, const std::map<std::string, std::string>& custom);

/// Reads the keys of one JSON object, remembering the first error and which keys were
/// consumed. After an error every further read is a no-op.
class ObjectReader {
public:
    /// `what` names the expected entity for the "expected an object" message, e.g. "a material".
    ObjectReader(const Json& json, std::string_view path, std::string_view what);

    bool ok() const noexcept { return !error_.has_value(); }
    const core::Error& error() const { return *error_; }
    std::string childPath(std::string_view key) const { return joinPath(path_, key); }
    void fail(const core::Error& error) {
        if (!error_) error_ = error;
    }
    void failAt(std::string_view key, const std::string& message) { fail(schemaError(childPath(key), message)); }

    /// The value of `key`, or nullptr when absent (a missing required key is an error).
    const Json* find(std::string_view key, bool required = false);

    void number(std::string_view key, double& out, NumberRule rule, bool required = false);
    void text(std::string_view key, std::string& out, bool required = false, bool nonEmpty = false);
    void boolean(std::string_view key, bool& out);

    template <class E> void enumeration(std::string_view key, E& out, bool required = false) {
        const Json* value = find(key, required);
        if (value == nullptr) return;
        if (value->is_string()) {
            if (const auto parsed = enumFromToken<E>(value->template get<std::string>())) {
                out = *parsed;
                return;
            }
        }
        failAt(key, "expected one of " + allowedTokens<E>() + ", got " + describe(*value));
    }

    /// decode(const Json&, std::string_view path) -> core::Result<T>
    template <class T, class Decode> void nested(std::string_view key, T& out, Decode decode, bool required = false) {
        const Json* value = find(key, required);
        if (value == nullptr) return;
        auto result = decode(*value, childPath(key));
        if (!result) return fail(result.error());
        out = std::move(result.value());
    }

    /// Optional array of entities; absent = empty.
    template <class T, class Decode> void array(std::string_view key, std::vector<T>& out, Decode decode) {
        const Json* value = find(key);
        if (value == nullptr) return;
        if (!value->is_array()) return failAt(key, "expected an array, got " + describe(*value));
        const std::string base = childPath(key);
        out.reserve(value->size());
        for (std::size_t i = 0; i < value->size(); ++i) {
            auto result = decode((*value)[i], joinPath(base, i));
            if (!result) return fail(result.error());
            out.push_back(std::move(result.value()));
        }
    }

    /// Reads the optional "custom" object (string -> string) and then stores every key
    /// not consumed so far as custom["json:<key>"]. Call last.
    void customAndUnknown(std::map<std::string, std::string>& custom);

    /// Fails on the first key that was not consumed. Call last.
    void rejectUnknown();

private:
    const Json* json_ = nullptr;
    std::string path_;
    std::vector<std::string> consumed_;
    std::optional<core::Error> error_;
};

/// Reports a duplicate among ids at `<base>/<i>/id`.
template <class T>
std::optional<core::Error> findDuplicateId(const std::vector<T>& items, const std::string& base) {
    std::map<std::string, std::size_t> seen;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto [it, inserted] = seen.emplace(items[i].id.str(), i);
        if (!inserted)
            return schemaError(joinPath(joinPath(base, i), "id"),
                               "duplicate id \"" + items[i].id.str() + "\" (already used by " +
                                   joinPath(base, it->second) + ")");
    }
    return std::nullopt;
}

} // namespace fos::formats::detail
