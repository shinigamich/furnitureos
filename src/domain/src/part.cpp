// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/domain/part.hpp"

#include "fos/domain/hardware.hpp"
#include "fos/domain/material.hpp"

namespace fos::domain {

double edgeLength(const Part& part, EdgeSide s) noexcept {
    return (s == EdgeSide::L1 || s == EdgeSide::L2) ? part.length : part.width;
}

Face faceOf(const Operation& op) noexcept {
    return std::visit([](const auto& s) { return s.face; }, op.shape);
}

const char* toString(Face f) noexcept {
    switch (f) {
    case Face::A: return "A";
    case Face::B: return "B";
    case Face::L1: return "L1";
    case Face::L2: return "L2";
    case Face::W1: return "W1";
    case Face::W2: return "W2";
    }
    return "?";
}

const char* toString(PartRole r) noexcept {
    switch (r) {
    case PartRole::Side: return "Side";
    case PartRole::Top: return "Top";
    case PartRole::Bottom: return "Bottom";
    case PartRole::Shelf: return "Shelf";
    case PartRole::Partition: return "Partition";
    case PartRole::Back: return "Back";
    case PartRole::Door: return "Door";
    case PartRole::DrawerFront: return "DrawerFront";
    case PartRole::DrawerSide: return "DrawerSide";
    case PartRole::DrawerBack: return "DrawerBack";
    case PartRole::DrawerBottom: return "DrawerBottom";
    case PartRole::Plinth: return "Plinth";
    case PartRole::Worktop: return "Worktop";
    case PartRole::Rail: return "Rail";
    case PartRole::Generic: return "Generic";
    }
    return "?";
}

const char* toString(EdgeSide s) noexcept {
    switch (s) {
    case EdgeSide::L1: return "L1";
    case EdgeSide::L2: return "L2";
    case EdgeSide::W1: return "W1";
    case EdgeSide::W2: return "W2";
    }
    return "?";
}

const char* toString(GrainDirection g) noexcept {
    switch (g) {
    case GrainDirection::None: return "None";
    case GrainDirection::AlongLength: return "AlongLength";
    case GrainDirection::AlongWidth: return "AlongWidth";
    }
    return "?";
}

const char* toString(MaterialKind k) noexcept {
    switch (k) {
    case MaterialKind::Particleboard: return "Particleboard";
    case MaterialKind::Mdf: return "Mdf";
    case MaterialKind::Hdf: return "Hdf";
    case MaterialKind::Plywood: return "Plywood";
    case MaterialKind::SolidWood: return "SolidWood";
    case MaterialKind::Other: return "Other";
    }
    return "?";
}

const char* toString(EdgeBandKind k) noexcept {
    switch (k) {
    case EdgeBandKind::Abs: return "Abs";
    case EdgeBandKind::Pvc: return "Pvc";
    case EdgeBandKind::Melamine: return "Melamine";
    case EdgeBandKind::Veneer: return "Veneer";
    case EdgeBandKind::SolidWood: return "SolidWood";
    case EdgeBandKind::Other: return "Other";
    }
    return "?";
}

const char* toString(HardwareKind k) noexcept {
    switch (k) {
    case HardwareKind::Hinge: return "Hinge";
    case HardwareKind::MountingPlate: return "MountingPlate";
    case HardwareKind::DrawerSlide: return "DrawerSlide";
    case HardwareKind::Handle: return "Handle";
    case HardwareKind::ShelfSupport: return "ShelfSupport";
    case HardwareKind::Confirmat: return "Confirmat";
    case HardwareKind::Dowel: return "Dowel";
    case HardwareKind::CamConnector: return "CamConnector";
    case HardwareKind::Screw: return "Screw";
    case HardwareKind::Foot: return "Foot";
    case HardwareKind::Leg: return "Leg";
    case HardwareKind::Suspension: return "Suspension";
    case HardwareKind::SlidingSystem: return "SlidingSystem";
    case HardwareKind::LiftMechanism: return "LiftMechanism";
    case HardwareKind::Other: return "Other";
    }
    return "?";
}

} // namespace fos::domain
