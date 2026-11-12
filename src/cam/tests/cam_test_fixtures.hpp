// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Test data. The profile and tools are FICTIONAL numbers for tests; they describe no real machine.
#include "fos/cam/machine.hpp"
#include "fos/cam/tool.hpp"
#include "fos/domain/fixtures.hpp"

#include <stdexcept>

namespace fos::cam::test {

inline MachineProfile testProfile() {
    MachineProfile p;
    p.name = "Test router (fictional)";
    p.controllerLabel = "none";
    p.envelopeX = {0, 2500};
    p.envelopeY = {0, 1300};
    p.envelopeZ = {-5, 150};
    p.safeZ = 50;
    p.clearance = 5;
    p.maxFeed = 10000;
    p.maxRpm = 24000;
    p.rapidFeed = 20000;
    p.toolChangeSeconds = 10;
    p.maxSpoilboardPenetration = 1.0;
    return p;
}

inline Tool drill(std::string id, int n, double d, double flute) {
    Tool t;
    t.id = std::move(id);
    t.number = n;
    t.kind = ToolKind::Drill;
    t.diameter = d;
    t.fluteLength = flute;
    t.feedRate = 1500;
    t.plungeRate = 1500;
    t.rpm = 6000;
    return t;
}

inline Tool endMill(std::string id, int n, double d, double flute, double doc) {
    Tool t;
    t.id = std::move(id);
    t.number = n;
    t.kind = ToolKind::EndMill;
    t.diameter = d;
    t.fluteLength = flute;
    t.feedRate = 3000;
    t.plungeRate = 1000;
    t.rpm = 18000;
    t.maxDepthOfCut = doc;
    return t;
}

/// T1 drill 5 (flute 30), T2 drill 8 (flute 30), T3 drill 35 (flute 15), T4 end mill 6 (flute 22, DOC 3).
inline ToolLibrary testTools() {
    ToolLibrary lib;
    for (auto t : {drill("drill.5", 1, 5, 30), drill("drill.8", 2, 8, 30), drill("drill.35", 3, 35, 15),
                   endMill("em.6", 4, 6, 22, 3)})
        if (!lib.add(t)) throw std::logic_error("test tool rejected");
    return lib;
}

inline domain::Part leftSide() {
    const auto model = domain::fixtures::basicCabinet();
    for (const auto& a : model.assemblies)
        if (const auto* p = a.findPart(domain::PartId("cab1/side.left"))) return *p;
    throw std::logic_error("fixture has no side.left");
}

/// 600 x 400 x 18 test panel without operations.
inline domain::Part panel() {
    domain::Part p;
    p.id = domain::PartId("test.panel");
    p.name = "Test panel";
    p.length = 600;
    p.width = 400;
    p.thickness = 18;
    return p;
}

inline domain::Operation op(std::string id, domain::OperationShape shape) {
    domain::Operation o;
    o.id = domain::OperationId(std::move(id));
    o.shape = std::move(shape);
    return o;
}

} // namespace fos::cam::test
