// SPDX-License-Identifier: AGPL-3.0-only
// Translated texts of the library editor (LibraryText, library_editor.hpp).
#include "fos/app/library_editor.hpp"

namespace fos::app {

QString LibraryText::materialKind(domain::MaterialKind kind) {
    switch (kind) {
    case domain::MaterialKind::Particleboard: return tr("Particleboard");
    case domain::MaterialKind::Mdf: return tr("MDF");
    case domain::MaterialKind::Hdf: return tr("HDF");
    case domain::MaterialKind::Plywood: return tr("Plywood");
    case domain::MaterialKind::SolidWood: return tr("Solid wood");
    case domain::MaterialKind::Other: return tr("Other");
    case domain::MaterialKind::Glass: return tr("Glass");
    }
    return QString::fromLatin1(domain::toString(kind));
}

QString LibraryText::edgeBandKind(domain::EdgeBandKind kind) {
    switch (kind) {
    case domain::EdgeBandKind::Abs: return tr("ABS");
    case domain::EdgeBandKind::Pvc: return tr("PVC");
    case domain::EdgeBandKind::Melamine: return tr("Melamine");
    case domain::EdgeBandKind::Veneer: return tr("Veneer");
    case domain::EdgeBandKind::SolidWood: return tr("Solid wood");
    case domain::EdgeBandKind::Other: return tr("Other");
    }
    return QString::fromLatin1(domain::toString(kind));
}

QString LibraryText::hardwareKind(domain::HardwareKind kind) {
    using K = domain::HardwareKind;
    switch (kind) {
    case K::Hinge: return tr("Hinge");
    case K::MountingPlate: return tr("Mounting plate");
    case K::DrawerSlide: return tr("Drawer slide");
    case K::Handle: return tr("Handle");
    case K::ShelfSupport: return tr("Shelf support");
    case K::Confirmat: return tr("Confirmat screw");
    case K::Dowel: return tr("Dowel");
    case K::CamConnector: return tr("Cam connector");
    case K::Screw: return tr("Screw");
    case K::Foot: return tr("Foot");
    case K::Leg: return tr("Leg");
    case K::Suspension: return tr("Cabinet hanger");
    case K::SlidingSystem: return tr("Sliding door system");
    case K::LiftMechanism: return tr("Lift mechanism");
    case K::Other: return tr("Other");
    }
    return QString::fromLatin1(domain::toString(kind));
}

std::vector<std::pair<QString, domain::MaterialKind>> LibraryText::materialKinds() {
    using K = domain::MaterialKind;
    std::vector<std::pair<QString, K>> out;
    for (K k : {K::Particleboard, K::Mdf, K::Hdf, K::Plywood, K::SolidWood, K::Other}) out.emplace_back(materialKind(k), k);
    return out;
}

std::vector<std::pair<QString, domain::EdgeBandKind>> LibraryText::edgeBandKinds() {
    using K = domain::EdgeBandKind;
    std::vector<std::pair<QString, K>> out;
    for (K k : {K::Abs, K::Pvc, K::Melamine, K::Veneer, K::SolidWood, K::Other}) out.emplace_back(edgeBandKind(k), k);
    return out;
}

QString LibraryText::rowCaption(const LibraryRow& row) {
    QString text = tr("%1 (%2)").arg(QString::fromStdString(row.name), QString::fromStdString(row.id));
    if (!row.usedBy.empty()) text += tr(" — used by %n item(s)", nullptr, static_cast<int>(row.usedBy.size()));
    return text;
}

QString LibraryText::mergeSummary(const formats::MergeReport& report) {
    const auto n = [&](formats::MergeAction a) { return static_cast<int>(report.count(a)); };
    return tr("%1 added, %2 replaced, %3 renamed, %4 skipped")
        .arg(n(formats::MergeAction::Added))
        .arg(n(formats::MergeAction::Replaced))
        .arg(n(formats::MergeAction::Renamed))
        .arg(n(formats::MergeAction::Skipped));
}

QString LibraryText::newMaterialName() { return tr("New material"); }
QString LibraryText::newEdgeBandName() { return tr("New edge band"); }
QString LibraryText::copyName(const std::string& name) { return tr("%1 (copy)").arg(QString::fromStdString(name)); }

} // namespace fos::app
