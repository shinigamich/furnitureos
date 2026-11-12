// SPDX-License-Identifier: AGPL-3.0-only
/// Checks 5 (machining, mating-hole coaxiality) and 6 (hardware).
#include "detail.hpp"

#include "fos/machining/face.hpp"
#include "fos/machining/validation.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace fos::validation::detail {
namespace {
using core::Severity;
using geometry::Vec3;

struct HoleRef {
    const domain::Part* part;
    const domain::Operation* op;
    machining::HoleAxis axis; ///< Assembly frame.
};

void checkMatingHoles(Context& c, const domain::Assembly& a) {
    const ValidationOptions& o = c.options;
    std::map<std::string, std::vector<HoleRef>> bySource; // ordered: deterministic
    for (const auto& p : a.parts) {
        if (!validSize(p) || !validPlacement(p.placement)) continue;
        for (const auto& op : p.operations) {
            const auto* h = std::get_if<domain::Hole>(&op.shape);
            if (!h || op.source == "user" || op.source.empty()) continue;
            const machining::HoleAxis ax = machining::holeAxisInAssembly(p, *h);
            if (!finite(ax.start) || !finite(ax.direction) || !finite(ax.length) || ax.length <= 0) continue;
            bySource[op.source].push_back({&p, &op, ax});
        }
    }
    const double cosTol = std::cos(core::degToRad(o.matingParallelToleranceDeg));
    for (const auto& [source, holes] : bySource) {
        for (std::size_t i = 0; i < holes.size(); ++i) {
            for (std::size_t j = i + 1; j < holes.size(); ++j) {
                const HoleRef& x = holes[i];
                const HoleRef& y = holes[j];
                if (x.part == y.part) continue;
                const Vec3 dx = x.axis.direction, dy = y.axis.direction;
                const double cosAngle = dot(dx, dy);
                if (std::fabs(cosAngle) < cosTol) continue;
                // Distance of the midpoint of y's drilled segment from the axis line of x.
                const Vec3 mid = y.axis.start + dy * (y.axis.length * 0.5);
                const Vec3 w = mid - x.axis.start;
                const double dist = length(w - dx * dot(w, dx));
                if (!(dist < o.matingSearchRadius)) continue;
                // Axial intervals along x's axis: x occupies [0, Lx].
                const double t1 = dot(y.axis.start - x.axis.start, dx);
                const double t2 = t1 + y.axis.length * cosAngle;
                const double lo = std::min(t1, t2), hi = std::max(t1, t2);
                const double gap = std::max({lo - x.axis.length, -hi, 0.0});
                if (gap > o.matingMaxAxialGap) continue;
                ++c.report.stats.matingHolePairs;
                if (dist <= o.coaxialTolerance) continue;
                c.add(Severity::Error, "validation.machining.mating_holes_not_coaxial",
                      "Hole '" + x.op->id.str() + "' on " + label(*x.part) + " and hole '" + y.op->id.str() + "' on " +
                          label(*y.part) + " (both from '" + source + "') should be coaxial but their axes are " +
                          num(dist) + " mm apart (tolerance " + num(o.coaxialTolerance) + " mm).",
                      x.part->id.str(), a.id.str(), x.part->id.str(), {y.part->id.str(), x.op->id.str(), y.op->id.str()});
            }
        }
    }
}
} // namespace

void checkMachining(Context& c) {
    for (const auto& a : c.model.assemblies) {
        for (const auto& p : a.parts) {
            if (!validSize(p)) continue; // reported as validation.part.invalid_size
            for (const core::Diagnostic& d : machining::validateOperations(p, c.options.machining))
                c.add(d.severity, d.code, d.message, d.entityId.empty() ? p.id.str() : d.entityId, a.id.str(),
                      p.id.str());
        }
        checkMatingHoles(c, a);
    }
}

void checkHardware(Context& c) {
    std::map<std::string, std::size_t> used; // definition id -> number of instances
    for (const auto& a : c.model.assemblies) {
        for (const auto& h : a.hardware) {
            const std::string hid = h.id.str(), aid = a.id.str();
            const domain::HardwareDefinition* def = c.model.findHardware(h.definition);
            if (!def)
                c.add(Severity::Error, "validation.hardware.unknown_definition",
                      "Hardware instance '" + hid + "' refers to definition '" + h.definition.str() +
                          "', which is not in the model's hardware catalogue.",
                      hid, aid, {}, {h.definition.str()});
            else
                ++used[def->id.str()];
            if (h.quantity <= 0)
                c.add(Severity::Error, "validation.hardware.invalid_quantity",
                      "Hardware instance '" + hid + "' has quantity " + std::to_string(h.quantity) +
                          "; it must be at least 1.",
                      hid, aid);
            for (const auto& pid : h.parts)
                if (!a.findPart(pid))
                    c.add(Severity::Error, "validation.hardware.unknown_part",
                          "Hardware instance '" + hid + "' refers to part '" + pid.str() + "', which is not in " +
                              label(a) + ".",
                          hid, aid, {}, {pid.str()});
        }
    }
    for (const auto& [id, n] : used) {
        const domain::HardwareDefinition* def = c.model.findHardware(domain::HardwareId(id));
        if (!def || def->dimensionsVerified) continue;
        c.add(Severity::Info, "validation.hardware.unverified_dimensions",
              "Hardware '" + def->name + "' (" + id + ", used by " + std::to_string(n) +
                  " instance(s)) has dimensions that were not verified against a manufacturer data sheet; check the "
                  "drilling before production.",
              id);
    }
}

} // namespace fos::validation::detail
