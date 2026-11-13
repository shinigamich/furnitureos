// SPDX-License-Identifier: AGPL-3.0-only
// FICTIONAL demo machine and tools (see demo_cam.hpp). Nothing here describes real equipment.
#include "fos/cli/demo_cam.hpp"

#include "fos/core/units.hpp"

#include <stdexcept>
#include <string>

namespace fos::cli {
namespace {

cam::Tool drill(int number, double diameter, double flute, double plunge, double rpm) {
    cam::Tool t;
    t.id = "demo.drill." + core::formatLength(diameter);
    t.number = number;
    t.kind = cam::ToolKind::Drill;
    t.description = "Demo drill " + core::formatLength(diameter) + " mm (fictional)";
    t.diameter = diameter;
    t.fluteLength = flute;
    t.feedRate = plunge;
    t.plungeRate = plunge;
    t.rpm = rpm;
    return t;
}

cam::Tool endMill(int number, double diameter, double flute, double depthOfCut) {
    cam::Tool t;
    t.id = "demo.endmill." + core::formatLength(diameter);
    t.number = number;
    t.kind = cam::ToolKind::EndMill;
    t.description = "Demo end mill " + core::formatLength(diameter) + " mm (fictional)";
    t.diameter = diameter;
    t.fluteLength = flute;
    t.feedRate = 3000;
    t.plungeRate = 1000;
    t.rpm = 18000;
    t.maxDepthOfCut = depthOfCut;
    t.centerCutting = true;
    return t;
}

} // namespace

cam::MachineProfile demoMachineProfile() {
    cam::MachineProfile p;
    p.name = kDemoProfileName;
    p.controllerLabel = "fictional controller (demo profile — not a real machine)";
    p.verified = false;
    p.verificationNote = "Demo profile — not a real machine. Not verified against any physical machine or controller.";
    p.envelopeX = {0, 3200};
    p.envelopeY = {0, 1600};
    p.envelopeZ = {-5, 150};
    p.safeZ = 60;
    p.clearance = 5;
    p.maxFeed = 12000;
    p.maxRpm = 24000;
    p.rapidFeed = 25000;
    p.toolChangeSeconds = 8;
    p.maxSpoilboardPenetration = 1.0;
    return p;
}

cam::ToolLibrary demoToolLibrary() {
    cam::ToolLibrary lib;
    const cam::Tool tools[] = {
        drill(1, 3, 30, 1500, 6000),  drill(2, 4, 30, 1500, 6000),  drill(3, 5, 30, 1500, 6000),
        drill(4, 7, 30, 1500, 6000),  drill(5, 8, 35, 1500, 6000),  drill(6, 10, 35, 1500, 6000),
        drill(7, 12, 35, 1500, 6000), drill(8, 15, 20, 1500, 6000), drill(9, 20, 20, 1500, 6000),
        drill(10, 35, 15, 800, 3000), endMill(11, 4, 22, 3),        endMill(12, 6, 22, 3),
        endMill(13, 8, 25, 4),
    };
    for (const auto& t : tools) {
        // The data above is constant and valid; a rejection is a programming error.
        if (auto r = lib.add(t); !r) throw std::logic_error("demo tool rejected: " + r.error().message);
    }
    return lib;
}

} // namespace fos::cli
