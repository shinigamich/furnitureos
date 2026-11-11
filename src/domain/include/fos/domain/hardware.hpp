// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/ids.hpp"
#include "fos/geometry/vec.hpp"

#include <map>
#include <string>
#include <vector>

namespace fos::domain {

enum class HardwareKind {
    Hinge, MountingPlate, DrawerSlide, Handle, ShelfSupport, Confirmat, Dowel, CamConnector,
    Screw, Foot, Leg, Suspension, SlidingSystem, LiftMechanism, Other
};

/// Catalogue entry describing a fitting. Geometry that drives drilling lives in
/// `params` (mm), using keys documented by the hardware module for each kind.
struct HardwareDefinition {
    HardwareId id;
    HardwareKind kind = HardwareKind::Other;
    std::string name;
    std::string manufacturer;  ///< Empty for generic definitions.
    std::string article;
    /// True only when the dimensions were checked against the manufacturer's published
    /// data sheet. Generic definitions are never "verified".
    bool dimensionsVerified = false;
    double unitCost = 0.0;
    std::map<std::string, double> params;
    std::map<std::string, std::string> custom;
};

/// A fitting used in an assembly.
struct HardwareInstance {
    HardwareInstanceId id;
    HardwareId definition;
    int quantity = 1;
    std::vector<PartId> parts;      ///< Parts this fitting connects / is mounted on.
    geometry::Vec3 position;        ///< Reference point in the assembly frame.
    std::string note;
};

const char* toString(HardwareKind k) noexcept;

} // namespace fos::domain
