// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <compare>
#include <functional>
#include <string>
#include <utility>

namespace fos::core {

/// Strongly typed, stable string identifier.
///
/// * User-created entities receive a random UUID v4 (see newUuid()).
/// * Entities generated from a parametric definition receive a deterministic
///   path-like id ("<owner-id>/side.left") so that they keep their identity across
///   recalculation, save and reload. Drawings, labels and manual annotations refer
///   to parts by these ids.
template <class Tag> struct TypedId {
    std::string value;

    TypedId() = default;
    explicit TypedId(std::string v) : value(std::move(v)) {}

    bool empty() const noexcept { return value.empty(); }
    const std::string& str() const noexcept { return value; }
    friend auto operator<=>(const TypedId&, const TypedId&) = default;
};

/// Random RFC 4122 version-4 UUID, lower-case, e.g. "3f2b8c1e-...".
std::string newUuid();

template <class Tag> TypedId<Tag> newId() { return TypedId<Tag>(newUuid()); }

} // namespace fos::core

template <class Tag> struct std::hash<fos::core::TypedId<Tag>> {
    std::size_t operator()(const fos::core::TypedId<Tag>& id) const noexcept {
        return std::hash<std::string>{}(id.value);
    }
};
