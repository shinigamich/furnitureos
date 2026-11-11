// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/hardware.hpp"
#include "fos/domain/material.hpp"
#include "fos/domain/operation.hpp"
#include "fos/domain/part.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>

/// Stable file tokens of the domain enumerations. These lower-case strings are part of
/// the file format and never change once released; they are independent of the display
/// names returned by domain::toString().
namespace fos::formats {

template <class E> struct EnumToken {
    E value;
    std::string_view token;
};

/// Token table of an enumeration, in declaration order. Specialised for
/// domain::MaterialKind, EdgeBandKind, HardwareKind, PartRole, EdgeSide, GrainDirection, Face.
template <class E> std::span<const EnumToken<E>> enumTokens() noexcept;

template <> std::span<const EnumToken<domain::MaterialKind>> enumTokens<domain::MaterialKind>() noexcept;
template <> std::span<const EnumToken<domain::EdgeBandKind>> enumTokens<domain::EdgeBandKind>() noexcept;
template <> std::span<const EnumToken<domain::HardwareKind>> enumTokens<domain::HardwareKind>() noexcept;
template <> std::span<const EnumToken<domain::PartRole>> enumTokens<domain::PartRole>() noexcept;
template <> std::span<const EnumToken<domain::EdgeSide>> enumTokens<domain::EdgeSide>() noexcept;
template <> std::span<const EnumToken<domain::GrainDirection>> enumTokens<domain::GrainDirection>() noexcept;
template <> std::span<const EnumToken<domain::Face>> enumTokens<domain::Face>() noexcept;

/// File token of `value`; empty for a value outside the enumeration.
template <class E> std::string_view enumToToken(E value) noexcept {
    for (const auto& entry : enumTokens<E>())
        if (entry.value == value) return entry.token;
    return {};
}

/// Exact (case-sensitive) lookup; nullopt for an unknown token.
template <class E> std::optional<E> enumFromToken(std::string_view token) noexcept {
    for (const auto& entry : enumTokens<E>())
        if (entry.token == token) return entry.value;
    return std::nullopt;
}

/// All tokens as `"a", "b", "c"` for error messages.
template <class E> std::string allowedTokens() {
    std::string out;
    for (const auto& entry : enumTokens<E>()) {
        if (!out.empty()) out += ", ";
        out += '"';
        out += entry.token;
        out += '"';
    }
    return out;
}

} // namespace fos::formats
