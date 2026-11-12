// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/cam/machine.hpp"

#include "fos/core/units.hpp"

namespace fos::cam {

namespace {
std::string mm(double v) { return core::formatLength(v, 3); }
} // namespace

core::Diagnostics validateProfile(const MachineProfile& p) {
    core::Diagnostics out;
    const std::string who = "Machine profile '" + p.name + "'";
    auto err = [&](const char* code, std::string msg) {
        out.push_back({core::Severity::Error, code, std::move(msg), p.name});
    };
    const std::pair<const char*, const machining::Range*> axes[] = {
        {"X", &p.envelopeX}, {"Y", &p.envelopeY}, {"Z", &p.envelopeZ}};
    for (const auto& [axis, r] : axes)
        if (!(r->max > r->min))
            err("cam.profile.invalid_envelope", who + " has an empty " + axis + " envelope [" + mm(r->min) + ", " +
                                                    mm(r->max) + "] mm.");
    if (!p.envelopeZ.contains(p.safeZ))
        err("cam.profile.safe_z_outside_envelope", who + ": safe Z " + mm(p.safeZ) + " mm is outside the Z envelope [" +
                                                       mm(p.envelopeZ.min) + ", " + mm(p.envelopeZ.max) + "] mm.");
    auto positive = [&](double v, const char* what) {
        if (!(v > 0)) err("cam.profile.invalid_limit", who + " has " + what + " " + mm(v) + "; it must be greater than 0.");
    };
    auto nonNegative = [&](double v, const char* what) {
        if (!(v >= 0)) err("cam.profile.invalid_limit", who + " has " + what + " " + mm(v) + "; it must not be negative.");
    };
    positive(p.maxFeed, "max feed");
    positive(p.maxRpm, "max spindle speed");
    positive(p.rapidFeed, "rapid feed");
    nonNegative(p.clearance, "clearance");
    nonNegative(p.maxSpoilboardPenetration, "max spoilboard penetration");
    nonNegative(p.toolChangeSeconds, "tool change time");
    if (!p.verified)
        out.push_back({core::Severity::Warning, "cam.profile.unverified",
                       who + " (controller: '" + p.controllerLabel + "') is NOT verified: " + p.verificationNote,
                       p.name});
    return out;
}

} // namespace fos::cam
