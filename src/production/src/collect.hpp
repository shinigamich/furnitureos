// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/production/part_list.hpp"

#include <cmath>

/// Internal helpers shared by the report generators (not part of the public API).
namespace fos::production::detail {

struct PartRef {
    const domain::Assembly* assembly = nullptr;
    const domain::Part* part = nullptr;
    int number = 0; ///< Model-wide sequential number (see PartRow::number).
};

struct Collected {
    std::vector<const domain::Assembly*> assemblies; ///< In scope, model order.
    std::vector<PartRef> parts;                      ///< In scope, number order.
    core::Diagnostics diagnostics;                   ///< Unknown scope assembly.
};

Collected collect(const domain::Model& model, const Scope& scope);

/// Resolved part row without diagnostics.
PartRow makeRow(const domain::Model& model, const PartRef& ref);

/// "'Left side' (cab1/side.left)".
std::string describe(const domain::Part& part);

inline bool positiveFinite(double v) noexcept { return std::isfinite(v) && v > 0.0; }
/// a * b in m2 for mm inputs; 0 unless both are positive finite numbers.
inline double areaM2(double a, double b) noexcept {
    return positiveFinite(a) && positiveFinite(b) ? a * b / 1.0e6 : 0.0;
}

void warn(core::Diagnostics& out, std::string code, std::string message, std::string entityId);

} // namespace fos::production::detail
