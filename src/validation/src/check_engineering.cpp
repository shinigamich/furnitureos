// SPDX-License-Identifier: AGPL-3.0-only
/// Check 8 (engineering): HEURISTIC rules on the generated model — shelf sag, door hinge
/// count, drawer box depth vs cabinet and slide, tipping of tall units. Every finding is a
/// Warning or an Info; the assumptions are named in the message (README, check 8).
#include "detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace fos::validation::detail {
namespace {
using core::Severity;
using domain::HardwareKind;
using domain::MaterialKind;
using domain::PartRole;

constexpr double kGravity = 9.81;       ///< m/s^2, N per kg.
constexpr double kLengthTolerance = 0.1; ///< mm, rounding allowance for length comparisons.

std::string fmt(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

/// Generic typical bending modulus (MPa = N/mm^2) per board kind. ORDER-OF-MAGNITUDE values of
/// the kind found in board makers' data sheets and in the minimum requirements of EN 312
/// (particleboard) and EN 622-5 (MDF) — deliberately on the low side; NOT values of any
/// product. Override per material with custom "elastic_modulus_mpa".
std::optional<double> genericModulus(MaterialKind k) noexcept {
    switch (k) {
    case MaterialKind::Particleboard: return 2500.0;
    case MaterialKind::Mdf: return 3000.0;
    case MaterialKind::Hdf: return 4000.0;
    case MaterialKind::Plywood: return 6000.0;
    case MaterialKind::SolidWood: return 9000.0;
    case MaterialKind::Glass: return 70000.0; // soda-lime float glass, textbook value
    case MaterialKind::Other: return std::nullopt;
    }
    return std::nullopt;
}

/// Generic typical density (kg/m^3) per board kind, used only when Material::density is 0.
std::optional<double> genericDensity(MaterialKind k) noexcept {
    switch (k) {
    case MaterialKind::Particleboard: return 650.0;
    case MaterialKind::Mdf: return 750.0;
    case MaterialKind::Hdf: return 900.0;
    case MaterialKind::Plywood: return 600.0;
    case MaterialKind::SolidWood: return 600.0;
    case MaterialKind::Glass: return 2500.0;
    case MaterialKind::Other: return std::nullopt;
    }
    return std::nullopt;
}

struct Density {
    double kgPerM3 = 0;
    std::string source; ///< "material density" or "generic <kind> density"
};

std::optional<Density> densityOf(const domain::Material& m) {
    if (finite(m.density) && m.density > 0) return Density{m.density, "material density"};
    if (auto d = genericDensity(m.kind)) return Density{*d, std::string("generic ") + domain::toString(m.kind) + " density"};
    return std::nullopt;
}

/// Mass of a panel in kg (finished size; edge bands and machining ignored).
double panelMassKg(const domain::Part& p, double kgPerM3) noexcept {
    return p.length * p.width * p.thickness * 1e-9 * kgPerM3;
}

/// Parses a strictly positive finite number; nullopt on anything else.
std::optional<double> parsePositive(const std::string& s) {
    if (s.empty()) return std::nullopt;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    while (end && (*end == ' ' || *end == '\t')) ++end;
    if (!end || *end != '\0' || !std::isfinite(v) || v <= 0) return std::nullopt;
    return v;
}

const domain::HardwareDefinition* definitionOf(const Context& c, const domain::HardwareInstance& h) {
    return c.model.findHardware(h.definition);
}

bool references(const domain::HardwareInstance& h, const domain::PartId& id) {
    return std::find(h.parts.begin(), h.parts.end(), id) != h.parts.end();
}

// ---------------------------------------------------------------- shelf sag

void checkShelfSag(Context& c, const domain::Assembly& a, const domain::Part& p) {
    const EngineeringOptions& o = c.options.engineering;
    const domain::Material* mat = c.model.findMaterial(p.material);
    if (!mat) return; // validation.part.missing_material
    const bool useRatio = finite(o.sagLimitSpanRatio) && o.sagLimitSpanRatio > 0;
    const bool useAbs = finite(o.sagLimitMm) && o.sagLimitMm > 0;
    if (!useRatio && !useAbs) return;

    double e = 0;
    std::string eSource;
    if (auto it = mat->custom.find("elastic_modulus_mpa"); it != mat->custom.end()) {
        const auto v = parsePositive(it->second);
        if (!v) {
            c.add(Severity::Info, "validation.engineering.invalid_custom_value",
                  "Material '" + mat->id.str() + "' has custom elastic_modulus_mpa = '" + it->second +
                      "', which is not a positive number; the shelf sag of " + label(p) + " was not estimated.",
                  p.id.str(), a.id.str(), p.id.str(), {mat->id.str()});
            return;
        }
        e = *v;
        eSource = "material custom elastic_modulus_mpa";
    } else if (auto g = genericModulus(mat->kind)) {
        e = *g;
        eSource = std::string("generic typical value for ") + domain::toString(mat->kind) +
                  "; set material custom 'elastic_modulus_mpa' for your board";
    } else {
        c.add(Severity::Info, "validation.engineering.unknown_modulus",
              "The shelf sag of " + label(p) + " was not estimated: material '" + mat->id.str() +
                  "' is of kind Other and has no custom 'elastic_modulus_mpa' (MPa).",
              p.id.str(), a.id.str(), p.id.str(), {mat->id.str()});
        return;
    }

    const double span = p.length, depth = p.width, t = p.thickness;
    double loadN = std::max(0.0, finite(o.shelfLoadKg) ? o.shelfLoadKg : 0.0) * kGravity;
    std::string selfText;
    if (o.shelfSelfWeight) {
        if (auto d = densityOf(*mat)) {
            const double kg = panelMassKg(p, d->kgPerM3);
            loadN += kg * kGravity;
            selfText = " + " + fmt(kg, 1) + " kg self weight (" + fmt(d->kgPerM3, 0) + " kg/m3, " + d->source + ")";
        } else {
            selfText = " (self weight unknown: no density)";
        }
    }
    const double inertia = depth * t * t * t / 12.0;                                      // mm^4
    const double sag = 5.0 * loadN * span * span * span / (384.0 * e * inertia);          // mm
    double limit = std::numeric_limits<double>::infinity();
    std::string limitText;
    if (useRatio) limit = span / o.sagLimitSpanRatio;
    if (useAbs) limit = std::min(limit, o.sagLimitMm);
    if (useRatio && useAbs)
        limitText = "min(span / " + fmt(o.sagLimitSpanRatio, 0) + ", " + num(o.sagLimitMm) + " mm)";
    else if (useRatio)
        limitText = "span / " + fmt(o.sagLimitSpanRatio, 0);
    else
        limitText = num(o.sagLimitMm) + " mm";
    if (!(sag > limit)) return;
    c.add(Severity::Warning, "validation.engineering.shelf_sag",
          label(p) + " (span " + num(span) + " mm, " + num(depth) + " mm deep, " + num(t) +
              " mm thick): estimated mid-span sag " + fmt(sag, 2) + " mm exceeds the limit of " + fmt(limit, 2) +
              " mm (" + limitText + ") under " + fmt(o.shelfLoadKg, 1) + " kg uniform load" + selfText +
              ". Heuristic: simply supported over its length, delta = 5WL^3/(384EI), E = " + fmt(e, 0) + " MPa (" +
              eSource + "), no creep; use a thicker or stiffer shelf, a shorter span or a centre support.",
          p.id.str(), a.id.str(), p.id.str(), {mat->id.str()});
}

// ---------------------------------------------------------------- hinge count

void checkHinges(Context& c, const domain::Assembly& a, const domain::Part& door) {
    const EngineeringOptions& o = c.options.engineering;
    if (!validPlacement(door.placement)) return; // reported by checkParts
    int hinges = 0;
    bool flap = false;
    for (const auto& h : a.hardware) {
        const domain::HardwareDefinition* def = definitionOf(c, h);
        if (!def || def->kind != HardwareKind::Hinge || !references(h, door.id) || h.quantity <= 0) continue;
        hinges += h.quantity;
        // A hinge carried by a horizontal panel (thickness axis along Z) hangs a top-hinged flap.
        for (const auto& id : h.parts) {
            const domain::Part* carrier = a.findPart(id);
            if (carrier && carrier != &door && validPlacement(carrier->placement) &&
                std::fabs(std::fabs(carrier->placement.zAxis.z) - 1.0) < 1e-6)
                flap = true;
        }
    }
    if (hinges == 0) return; // sliding / lift / pivot doors are not judged
    const geometry::Vec3 ext = door.bounds().size();
    const double edge = flap ? ext.x : ext.z;
    const char* edgeName = flap ? "width (top-hinged flap)" : "height";

    // Height term from the table (ascending maxDoorHeight).
    std::vector<HingeHeightRule> table = o.hingeHeightTable;
    std::sort(table.begin(), table.end(),
              [](const HingeHeightRule& x, const HingeHeightRule& y) { return x.maxDoorHeight < y.maxDoorHeight; });
    int byHeight = 0;
    for (const auto& r : table)
        if (edge <= r.maxDoorHeight + kLengthTolerance) {
            byHeight = r.hinges;
            break;
        }
    if (byHeight == 0 && !table.empty())
        c.add(Severity::Info, "validation.engineering.hinge_table_exceeded",
              "Door " + label(door) + " has a hinged " + edgeName + " of " + num(edge) +
                  " mm, beyond the hinge-count table (up to " + num(table.back().maxDoorHeight) +
                  " mm); check the hinge maker's chart.",
              door.id.str(), a.id.str(), door.id.str());

    // Weight term.
    int byWeight = 0;
    std::string weightText;
    if (finite(o.doorKgPerHinge) && o.doorKgPerHinge > 0) {
        const domain::Material* mat = c.model.findMaterial(door.material);
        if (auto d = mat ? densityOf(*mat) : std::nullopt) {
            const double kg = panelMassKg(door, d->kgPerM3);
            byWeight = std::max(2, static_cast<int>(std::ceil(kg / o.doorKgPerHinge - 1e-9)));
            weightText = ", weight about " + fmt(kg, 1) + " kg (" + fmt(d->kgPerM3, 0) + " kg/m3, " + d->source +
                         ") -> " + std::to_string(byWeight) + " at " + fmt(o.doorKgPerHinge, 1) + " kg per hinge";
        } else {
            weightText = ", weight unknown (no density)";
        }
    }
    const int required = std::max(byHeight, byWeight);
    if (hinges >= required) return;
    c.add(Severity::Warning, "validation.engineering.too_few_hinges",
          "Door " + label(door) + " hangs on " + std::to_string(hinges) + " hinge(s) but about " +
              std::to_string(required) + " are suggested: " + edgeName + " " + num(edge) + " mm -> " +
              std::to_string(byHeight) + " by the height table" + weightText +
              ". Rule of thumb, check the hinge maker's chart.",
          door.id.str(), a.id.str(), door.id.str());
}

// ---------------------------------------------------------------- drawer depth

/// Slide length of a DrawerSlide instance: definition param "length" (fixed-length
/// definition) or the "slide length <L> mm" note written by hardware::placeDrawerSlides.
std::optional<double> slideLength(const domain::HardwareInstance& h, const domain::HardwareDefinition& def) {
    if (auto it = def.params.find("length"); it != def.params.end() && finite(it->second) && it->second > 0)
        return it->second;
    static const std::string key = "slide length ";
    const auto pos = h.note.find(key);
    if (pos == std::string::npos) return std::nullopt;
    const char* begin = h.note.c_str() + pos + key.size();
    char* end = nullptr;
    const double v = std::strtod(begin, &end);
    if (end == begin || !std::isfinite(v) || v <= 0) return std::nullopt;
    return v;
}

void checkDrawers(Context& c, const domain::Assembly& a) {
    const EngineeringOptions& o = c.options.engineering;
    // Cabinet interior along Y: front = front edge of the sides, rear = front face of the back
    // panel (when it lies behind that front edge), else the rear edge of the sides.
    double front = std::numeric_limits<double>::infinity(), sideRear = -front, backFront = front;
    for (const auto& p : a.parts) {
        if (!validSize(p) || !validPlacement(p.placement)) continue;
        const geometry::Aabb b = p.bounds();
        if (p.role == PartRole::Side) {
            front = std::min(front, b.min.y);
            sideRear = std::max(sideRear, b.max.y);
        } else if (p.role == PartRole::Back) {
            backFront = std::min(backFront, b.min.y);
        }
    }
    if (!std::isfinite(front)) return; // no carcass sides
    const bool viaBack = std::isfinite(backFront) && backFront > front && backFront <= sideRear + kLengthTolerance;
    const double rear = viaBack ? backFront : sideRear;
    const double interior = rear - front;
    const std::string interiorText = "interior depth " + num(interior) + " mm (front edge of the sides to " +
                                     (viaBack ? std::string("the back panel") : std::string("the rear of the sides")) + ")";

    for (const auto& p : a.parts) {
        if (p.role != PartRole::DrawerSide || !validSize(p) || !validPlacement(p.placement)) continue;
        const double box = p.bounds().size().y;
        if (box + o.drawerMinRearClearance > interior + kLengthTolerance)
            c.add(Severity::Warning, "validation.engineering.drawer_too_deep",
                  "Drawer side " + label(p) + " is " + num(box) + " mm long, but the " + interiorText +
                      (o.drawerMinRearClearance > 0 ? " leaves only " + num(interior - o.drawerMinRearClearance) +
                                                          " mm after the required rear clearance of " +
                                                          num(o.drawerMinRearClearance) + " mm"
                                                    : std::string()) +
                      ". Heuristic: the drawer runs along Y.",
                  p.id.str(), a.id.str(), p.id.str());
        for (const auto& h : a.hardware) {
            const domain::HardwareDefinition* def = definitionOf(c, h);
            if (!def || def->kind != HardwareKind::DrawerSlide || !references(h, p.id)) continue;
            const auto len = slideLength(h, *def);
            if (!len) continue;
            if (*len > interior + kLengthTolerance)
                c.add(Severity::Warning, "validation.engineering.slide_too_long",
                      "Drawer slide '" + h.id.str() + "' is " + num(*len) + " mm long, more than the " + interiorText +
                          ".",
                      p.id.str(), a.id.str(), p.id.str(), {h.id.str()});
            if (box > *len + kLengthTolerance)
                c.add(Severity::Warning, "validation.engineering.drawer_longer_than_slide",
                      "Drawer side " + label(p) + " is " + num(box) + " mm long, longer than its slide '" + h.id.str() +
                          "' (" + num(*len) + " mm); boxes are normally at most the nominal slide length.",
                      p.id.str(), a.id.str(), p.id.str(), {h.id.str()});
        }
    }
}

// ---------------------------------------------------------------- tipping

void checkTipping(Context& c, const domain::Assembly& a) {
    const EngineeringOptions& o = c.options.engineering;
    geometry::Aabb box;
    for (const auto& p : a.parts) {
        if (!validSize(p) || !validPlacement(p.placement)) continue;
        const geometry::Aabb b = p.bounds();
        box.expand(b.min);
        box.expand(b.max);
    }
    if (box.empty()) return;
    const geometry::Vec3 s = box.size();
    if (!(s.y > 0) || s.z < o.tippingMinHeight) return;
    const double ratio = s.z / s.y;
    if (!(ratio > o.tippingRatio)) return;
    for (const auto& h : a.hardware) {
        const domain::HardwareDefinition* def = definitionOf(c, h);
        if (!def) continue;
        if (def->kind == HardwareKind::Suspension) return;
        if (auto it = def->custom.find("wall_fixing"); it != def->custom.end() && it->second == "true") return;
    }
    c.add(Severity::Warning, "validation.engineering.tipping_risk",
          label(a) + " is " + num(s.z) + " mm tall and " + num(s.y) + " mm deep (height/depth " + fmt(ratio, 2) +
              " > " + fmt(o.tippingRatio, 2) +
              ") and has no wall fixing hardware (no Suspension fitting or definition with custom wall_fixing=true); "
              "fix it to the wall with an anti-tip fitting. Heuristic, not a stability calculation.",
          a.id.str(), a.id.str());
}

} // namespace

void checkEngineering(Context& c) {
    const EngineeringOptions& o = c.options.engineering;
    for (const auto& a : c.model.assemblies) {
        for (const auto& p : a.parts) {
            if (!validSize(p)) continue; // reported by checkParts
            if (o.shelfSag && p.role == PartRole::Shelf) checkShelfSag(c, a, p);
            if (o.hingeCount && p.role == PartRole::Door) checkHinges(c, a, p);
        }
        if (o.drawerDepth) checkDrawers(c, a);
        if (o.tipping) checkTipping(c, a);
    }
}

} // namespace fos::validation::detail
