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
    int boughtInExcluded = 0; ///< In-scope bought-in parts left out (PartSet::Workshop only).
};

/// Which parts collect() returns.
enum class PartSet {
    All,      ///< Every part (part list, packing, assembly sheets).
    Workshop  ///< Without bought-in parts (domain::isBoughtIn): cut list, banding, materials.
};

/// THE single place where production drops bought-in parts (glass panes) from the workshop
/// outputs; part numbers stay model-wide either way.
Collected collect(const domain::Model& model, const Scope& scope, PartSet set = PartSet::All);

/// Appends Info production.bought_in.excluded ("N bought-in part(s) ... are not in the <what>")
/// when c.boughtInExcluded > 0.
void noteBoughtIn(core::Diagnostics& out, const Collected& c, const std::string& what);

/// The part list (rows + per-part diagnostics) of the collected parts; the public
/// production::partList() is partList(model, collect(model, scope)).
PartList partList(const domain::Model& model, Collected c);

/// Edge band reference resolved against the model (raw cell for nullopt).
EdgeCell resolveEdge(const domain::Model& model, const std::optional<domain::EdgeBandId>& e);

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
