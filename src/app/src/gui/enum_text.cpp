// SPDX-License-Identifier: AGPL-3.0-only
#include "enum_text.hpp"

#include <QCoreApplication>

namespace fos::app::gui {
namespace {
QString t(const char* text) { return QCoreApplication::translate("fos::app::gui::EnumText", text); }
} // namespace

QString roleText(domain::PartRole role) {
    using R = domain::PartRole;
    switch (role) {
    case R::Side: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Side"));
    case R::Top: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Top"));
    case R::Bottom: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Bottom"));
    case R::Shelf: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Shelf"));
    case R::Partition: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Partition"));
    case R::Back: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Back panel"));
    case R::Door: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Door"));
    case R::DrawerFront: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Drawer front"));
    case R::DrawerSide: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Drawer side"));
    case R::DrawerBack: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Drawer back"));
    case R::DrawerBottom: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Drawer bottom"));
    case R::Plinth: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Plinth"));
    case R::Worktop: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Worktop"));
    case R::Rail: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Rail"));
    case R::Generic: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Panel"));
    }
    return {};
}

QString grainText(domain::GrainDirection grain) {
    switch (grain) {
    case domain::GrainDirection::None: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "none"));
    case domain::GrainDirection::AlongLength: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "along length"));
    case domain::GrainDirection::AlongWidth: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "along width"));
    }
    return {};
}

QString severityText(core::Severity severity) {
    switch (severity) {
    case core::Severity::Error: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Error"));
    case core::Severity::Warning: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Warning"));
    case core::Severity::Info: return t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Info"));
    }
    return {};
}

Choices<furniture::JointKind> jointChoices() {
    return {{t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Between the sides")), furniture::JointKind::BetweenSides},
            {t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Over the sides")), furniture::JointKind::OverSides}};
}

Choices<furniture::TopKind> topChoices() {
    return {{t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Full panel")), furniture::TopKind::Panel},
            {t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Stretcher rails")), furniture::TopKind::Rails}};
}

Choices<furniture::BackMount> backChoices() {
    return {{t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "No back")), furniture::BackMount::None},
            {t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Overlay (on the rear)")), furniture::BackMount::Overlay},
            {t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "In grooves")), furniture::BackMount::Grooved},
            {t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Inset")), furniture::BackMount::Inset}};
}

Choices<furniture::PlinthKind> plinthChoices() {
    return {{t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "No plinth")), furniture::PlinthKind::None},
            {t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Plinth panel, sides to the floor")),
             furniture::PlinthKind::SidesToFloor},
            {t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Adjustable feet")), furniture::PlinthKind::Feet}};
}

Choices<furniture::JointFastener> fastenerChoices() {
    return {{t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "None")), furniture::JointFastener::None},
            {t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Confirmat screws")), furniture::JointFastener::Confirmat},
            {t(QT_TRANSLATE_NOOP("fos::app::gui::EnumText", "Dowels")), furniture::JointFastener::Dowel}};
}

} // namespace fos::app::gui
