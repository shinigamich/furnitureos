// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/fixtures.hpp"

#include <string>

/// Hand-built models shared by the production tests.
namespace fos::production::testing {

/// basicCabinet() plus an identical second cabinet "cab2" (part ids cab2/...),
/// placed 800 mm to the right. Parts 1-6 belong to cab1, 7-12 to cab2.
inline domain::Model twoCabinets() {
    domain::Model m = domain::fixtures::basicCabinet();
    domain::Assembly b = m.assemblies.front();
    b.id = domain::AssemblyId("cab2");
    b.name = "Base cabinet 800 (2)";
    b.placement = geometry::Transform::translation({800, 0, 0});
    auto rename = [](std::string s) { return s.replace(0, 4, "cab2"); };
    for (auto& p : b.parts) {
        p.id = domain::PartId(rename(p.id.str()));
        for (auto& op : p.operations) op.id = domain::OperationId(rename(op.id.str()));
    }
    for (auto& h : b.hardware) {
        h.id = domain::HardwareInstanceId(rename(h.id.str()));
        for (auto& pid : h.parts) pid = domain::PartId(rename(pid.str()));
    }
    m.assemblies.push_back(b);
    return m;
}

/// A bare panel in assembly "a1" of an otherwise empty model helper.
inline domain::Part panel(const std::string& id, const std::string& name, double l, double w, double t,
                          const std::string& material) {
    domain::Part p;
    p.id = domain::PartId(id);
    p.name = name;
    p.length = l;
    p.width = w;
    p.thickness = t;
    p.material = domain::MaterialId(material);
    return p;
}

/// One material "m1" (18 mm, sheet 1000 x 1000, no grain, 10.00 / sheet) and one empty assembly "a1".
inline domain::Model smallModel() {
    domain::Model m;
    domain::Material mat;
    mat.id = domain::MaterialId("m1");
    mat.name = "Board";
    mat.thickness = 18;
    mat.sheetLength = 1000;
    mat.sheetWidth = 1000;
    mat.costPerSheet = 10.0;
    m.materials = {mat};
    domain::Assembly a;
    a.id = domain::AssemblyId("a1");
    a.name = "Box";
    m.assemblies = {a};
    return m;
}

} // namespace fos::production::testing
