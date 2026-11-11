// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/domain/fixtures.hpp"

namespace fos::domain::fixtures {
namespace {
using geometry::Transform;
using geometry::Vec3;

Part panel(const char* id, const char* name, PartRole role, double l, double w, double t, const char* mat,
           GrainDirection grain, Transform placement) {
    Part p;
    p.id = PartId(std::string("cab1/") + id);
    p.name = name;
    p.role = role;
    p.length = l;
    p.width = w;
    p.thickness = t;
    p.material = MaterialId(mat);
    p.grain = grain;
    p.placement = placement;
    return p;
}
} // namespace

Model basicCabinet() {
    Model m;

    Material pb;
    pb.id = MaterialId("mat.pb18");
    pb.name = "Particleboard 18 mm";
    pb.kind = MaterialKind::Particleboard;
    pb.thickness = 18.0;
    pb.sheetLength = 2800.0;
    pb.sheetWidth = 2070.0;
    pb.hasGrain = true;
    pb.color = {222, 204, 170};
    pb.costPerSheet = 60.0;
    Material hdf;
    hdf.id = MaterialId("mat.hdf3");
    hdf.name = "HDF 3 mm";
    hdf.kind = MaterialKind::Hdf;
    hdf.thickness = 3.0;
    hdf.sheetLength = 2800.0;
    hdf.sheetWidth = 2070.0;
    hdf.hasGrain = false;
    hdf.color = {240, 240, 235};
    hdf.costPerSheet = 15.0;
    m.materials = {pb, hdf};

    EdgeBand abs2;
    abs2.id = EdgeBandId("eb.abs2");
    abs2.name = "ABS 2 mm";
    abs2.thickness = 2.0;
    abs2.width = 22.0;
    abs2.costPerMeter = 0.80;
    EdgeBand abs04;
    abs04.id = EdgeBandId("eb.abs04");
    abs04.name = "ABS 0.4 mm";
    abs04.thickness = 0.4;
    abs04.width = 22.0;
    abs04.costPerMeter = 0.30;
    m.edgeBands = {abs2, abs04};

    HardwareDefinition pin;
    pin.id = HardwareId("hw.shelf_pin_5");
    pin.kind = HardwareKind::ShelfSupport;
    pin.name = "Shelf support pin 5 mm (generic)";
    pin.unitCost = 0.05;
    pin.params = {{"hole_diameter", 5.0}, {"hole_depth", 13.0}};
    m.hardwareCatalog = {pin};

    Assembly a;
    a.id = AssemblyId("cab1");
    a.name = "Base cabinet 800";

    // Vertical panels: length -> +Z (up), width -> +Y (depth), thickness -> -X.
    // Face A (local z = T) of the left side is therefore the OUTER face (x = 0) and
    // face B the inner face (x = 18); for the right side A is the inner face (x = 782).
    const Vec3 up{0, 0, 1}, back{0, 1, 0}, right{1, 0, 0};
    const auto along = GrainDirection::AlongLength;

    Part left = panel("side.left", "Left side", PartRole::Side, 720, 560, 18, "mat.pb18", along,
                      Transform::fromAxes(up, back, {18, 0, 0}));
    Part rightSide = panel("side.right", "Right side", PartRole::Side, 720, 560, 18, "mat.pb18", along,
                           Transform::fromAxes(up, back, {800, 0, 0}));
    for (Part* s : {&left, &rightSide}) {
        s->edge(EdgeSide::L1) = abs2.id;
        s->edge(EdgeSide::W1) = abs04.id;
        s->edge(EdgeSide::W2) = abs04.id;
    }
    // Shelf-pin holes on the inner face (B) of the left side, supporting shelf.1 whose
    // underside is at z = 351: holes at height 346, 37 mm from the front and from the back.
    Operation h1;
    h1.id = OperationId("cab1/side.left/op.pin.front");
    h1.shape = Hole{Face::B, 346.0, 37.0, 5.0, 13.0, false};
    h1.source = "cab1/hw.shelf_pins";
    Operation h2;
    h2.id = OperationId("cab1/side.left/op.pin.back");
    h2.shape = Hole{Face::B, 346.0, 523.0, 5.0, 13.0, false};
    h2.source = "cab1/hw.shelf_pins";
    left.operations = {h1, h2};

    // Horizontal panels: length -> +X, width -> +Y, thickness -> +Z.
    Part bottom = panel("bottom", "Bottom", PartRole::Bottom, 764, 560, 18, "mat.pb18", along,
                        Transform::translation({18, 0, 0}));
    Part top = panel("top", "Top", PartRole::Top, 764, 560, 18, "mat.pb18", along,
                     Transform::translation({18, 0, 702}));
    Part shelf = panel("shelf.1", "Shelf", PartRole::Shelf, 764, 540, 18, "mat.pb18", along,
                       Transform::translation({18, 20, 351}));
    for (Part* p : {&bottom, &top, &shelf}) p->edge(EdgeSide::L1) = abs2.id;

    // Back: length -> +X, width -> +Z, thickness -> -Y; occupies y in [560, 563].
    Part backPanel = panel("back", "Back panel", PartRole::Back, 796, 716, 3, "mat.hdf3", GrainDirection::None,
                           Transform::fromAxes(right, up, {2, 563, 2}));

    a.parts = {left, rightSide, bottom, top, shelf, backPanel};

    HardwareInstance pins;
    pins.id = HardwareInstanceId("cab1/hw.shelf_pins");
    pins.definition = pin.id;
    pins.quantity = 4;
    pins.parts = {left.id, rightSide.id, shelf.id};
    pins.position = {18, 37, 346};
    pins.note = "Supports shelf.1";
    a.hardware = {pins};

    m.assemblies = {a};
    return m;
}

} // namespace fos::domain::fixtures
