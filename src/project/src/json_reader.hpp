// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Private strict JSON object reader of the project codecs (not installed).
#include "fos/core/result.hpp"
#include "fos/formats/json.hpp"
#include "fos/geometry/vec.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fos::project::detail {

using formats::Json;

std::string describe(const Json& value);
/// ParseError "<path>: <message>", context = path.
core::Error schemaError(std::string_view path, const std::string& message);
std::string joinPath(std::string_view path, std::string_view key);
std::string joinPath(std::string_view path, std::size_t index);

enum class Num { Any, Positive, NonNegative };

core::Result<double> readNumber(const Json& value, std::string_view path, Num rule);
core::Result<geometry::Vec2> readVec2(const Json& value, std::string_view path);
Json writeVec2(geometry::Vec2 v);

/// Reads the keys of one JSON object. The first error is remembered and every later read is a
/// no-op. Every key asked for (present or not) counts as known for unknown() / rejectUnknown().
class Reader {
public:
    Reader(const Json& json, std::string_view path, std::string_view what);

    bool ok() const noexcept { return !error_.has_value(); }
    const core::Error& error() const { return *error_; }
    const std::string& path() const noexcept { return path_; }
    std::string at(std::string_view key) const { return joinPath(path_, key); }
    void fail(const core::Error& e) {
        if (!error_) error_ = e;
    }
    void failAt(std::string_view key, const std::string& message) { fail(schemaError(at(key), message)); }

    /// Value of `key` or nullptr (an absent required key is an error).
    const Json* find(std::string_view key, bool required = false);

    void text(std::string_view key, std::string& out, bool required = false, bool nonEmpty = false);
    void number(std::string_view key, double& out, Num rule, bool required = false);
    /// Absent or null -> nullopt.
    void optionalNumber(std::string_view key, std::optional<double>& out, Num rule);
    /// Absent or null -> nullopt.
    void optionalText(std::string_view key, std::optional<std::string>& out, bool nonEmpty);
    void integer(std::string_view key, int& out, int minimum, bool required = false);
    void boolean(std::string_view key, bool& out);
    void vec2(std::string_view key, geometry::Vec2& out, bool required = false);

    /// `parse(std::string_view) -> std::optional<E>`; `allowed` lists the tokens for the message.
    template <class E, class Parse>
    void token(std::string_view key, E& out, Parse parse, const std::string& allowed, bool required = false) {
        const Json* v = find(key, required);
        if (v == nullptr) return;
        if (v->is_string())
            if (const auto e = parse(v->template get<std::string>())) {
                out = *e;
                return;
            }
        failAt(key, "expected one of " + allowed + ", got " + describe(*v));
    }

    /// decode(const Json&, std::string_view path) -> core::Result<T>
    template <class T, class Decode> void nested(std::string_view key, T& out, Decode decode, bool required = false) {
        const Json* v = find(key, required);
        if (v == nullptr) return;
        auto r = decode(*v, at(key));
        if (!r) return fail(r.error());
        out = std::move(r.value());
    }

    /// Optional array; absent = empty.
    template <class T, class Decode> void array(std::string_view key, std::vector<T>& out, Decode decode) {
        const Json* v = find(key);
        if (v == nullptr) return;
        if (!v->is_array()) return failAt(key, "expected an array, got " + describe(*v));
        const std::string base = at(key);
        for (std::size_t i = 0; i < v->size(); ++i) {
            auto r = decode((*v)[i], joinPath(base, i));
            if (!r) return fail(r.error());
            out.push_back(std::move(r.value()));
        }
    }

    /// Object of all keys not asked for so far (file order). Call last.
    Json unknown() const;
    /// Fails on the first key not asked for, naming the allowed keys. Call last.
    void rejectUnknown();

private:
    const Json* json_ = nullptr;
    std::string path_;
    std::vector<std::string> known_;
    std::optional<core::Error> error_;
};

/// Appends the keys of `extra` (preserved unknown keys) to `object`.
void appendExtra(Json& object, const Json& extra);

} // namespace fos::project::detail
