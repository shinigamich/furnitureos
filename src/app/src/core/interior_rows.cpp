// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/interior_rows.hpp"

#include "fos/app/cabinet_run_form.hpp"
#include "fos/app/inner_drawer_rows.hpp"
#include "fos/app/parameter_rows.hpp"
#include "fos/core/units.hpp"
#include "fos/hardware/catalog.hpp"

#include <QCoreApplication>
#include <QStringList>

namespace fos::app {

namespace {

class InteriorText {
    Q_DECLARE_TR_FUNCTIONS(InteriorText)
};

using furniture::SectionSize;
using furniture::SizeKind;
using furniture::SplitDirection;

QString number(double v) {
    return QString::fromStdString(core::formatLength(v, 3));
}

void collect(const furniture::Section& s, const std::string& path, int depth, int index, int siblings,
             SplitDirection parentSplit, std::vector<SectionRow>& out) {
    SectionRow row;
    row.path = path;
    row.depth = depth;
    row.index = index;
    row.siblings = siblings;
    row.parentSplit = parentSplit;
    row.size = s.size;
    row.split = s.split;
    row.childCount = static_cast<int>(s.children.size());
    row.shelfCount = s.shelfCount;
    row.front = s.front;
    row.hangingRail = s.hangingRail;
    row.innerDrawers = s.innerDrawers;
    out.push_back(row);
    if (s.split == SplitDirection::None)
        return;
    for (std::size_t k = 0; k < s.children.size(); ++k) {
        const std::string child = (path.empty() ? std::string() : path + ".") + std::to_string(k + 1);
        collect(s.children[k], child, depth + 1, static_cast<int>(k + 1), row.childCount, s.split, out);
    }
}

core::Error inputError(const QString& message) {
    return core::makeError(core::ErrorCode::InvalidArgument, message.toStdString());
}

} // namespace

std::vector<SectionRow> sectionRows(const furniture::Section& root) {
    std::vector<SectionRow> rows;
    collect(root, "", 0, 0, 0, SplitDirection::None, rows);
    return rows;
}

QString sectionCaption(const SectionRow& row) {
    if (row.isRoot())
        return InteriorText::tr("Whole interior");
    QString text = InteriorText::tr("Section %1").arg(QString::fromStdString(row.path));
    const bool first = row.index == 1;
    const bool last = row.index == row.siblings;
    if (row.parentSplit == SplitDirection::Vertical) {
        if (first)
            text += QLatin1Char(' ') + InteriorText::tr("(left)");
        else if (last)
            text += QLatin1Char(' ') + InteriorText::tr("(right)");
    } else if (row.parentSplit == SplitDirection::Horizontal) {
        if (first)
            text += QLatin1Char(' ') + InteriorText::tr("(bottom)");
        else if (last)
            text += QLatin1Char(' ') + InteriorText::tr("(top)");
    }
    return text;
}

QString sizeDescription(const SectionSize& size) {
    switch (size.kind) {
    case SizeKind::Fixed:
        return InteriorText::tr("%1 mm (fixed)").arg(number(size.value));
    case SizeKind::Weight:
        return InteriorText::tr("share %1").arg(number(size.value));
    case SizeKind::Expression:
        return QStringLiteral("= ") + QString::fromStdString(size.expression);
    }
    return {};
}

QString contentText(const SectionRow& row) {
    QString text;
    if (row.split == SplitDirection::Vertical)
        text = InteriorText::tr("%1 side by side").arg(row.childCount);
    else if (row.split == SplitDirection::Horizontal)
        text = InteriorText::tr("%1 stacked").arg(row.childCount);
    else if (row.shelfCount > 0)
        text = InteriorText::tr("shelves: %1").arg(row.shelfCount);
    else if (!row.hangingRail && !row.innerDrawers)
        text = InteriorText::tr("empty");
    if (row.isLeaf() && row.hangingRail)
        text = text.isEmpty() ? InteriorText::tr("clothes rail")
                              : text + QStringLiteral(", ") + InteriorText::tr("clothes rail");
    if (row.isLeaf() && row.innerDrawers)
        text = text.isEmpty() ? innerDrawersText(*row.innerDrawers)
                              : text + QStringLiteral(", ") + innerDrawersText(*row.innerDrawers);
    if (row.front) {
        QString front;
        switch (row.front->kind) {
        case furniture::FrontKind::Door:
            front = row.front->hingeSide == furniture::HingeSide::Left
                        ? InteriorText::tr("door, hinged left")
                        : InteriorText::tr("door, hinged right");
            break;
        case furniture::FrontKind::DoorPair:
            front = InteriorText::tr("door pair");
            break;
        case furniture::FrontKind::DrawerStack:
            front = InteriorText::tr("drawers: %1").arg(row.front->drawerHeights.size());
            break;
        case furniture::FrontKind::Flap:
            front = InteriorText::tr("lift-up flap");
            break;
        case furniture::FrontKind::SlidingDoors:
            front = row.front->sliding.countExpression.empty()
                        ? InteriorText::tr("sliding doors: %1").arg(row.front->sliding.count)
                        : InteriorText::tr("sliding doors: %1").arg(
                              QString::fromStdString(row.front->sliding.countExpression));
            break;
        case furniture::FrontKind::ApplianceNiche:
            front = InteriorText::tr("appliance niche");
            break;
        case furniture::FrontKind::ApplianceFront:
            front = InteriorText::tr("appliance decor front");
            break;
        }
        if (row.front->style == furniture::FrontStyle::FramePanel)
            front += row.front->frame.panelKind == furniture::FramePanelKind::Glass
                         ? InteriorText::tr(", frame with glass")
                         : InteriorText::tr(", frame and panel");
        text += QStringLiteral("; ") + front;
    }
    return text;
}

QString sizeText(const SectionSize& size) {
    switch (size.kind) {
    case SizeKind::Fixed:
        return number(size.value);
    case SizeKind::Weight:
        return number(size.value) + QLatin1Char('*');
    case SizeKind::Expression:
        return QString::fromStdString(size.expression);
    }
    return {};
}

core::Result<SectionSize> parseSectionSize(const QString& text) {
    const QString t = text.trimmed();
    if (t.isEmpty())
        return inputError(
            InteriorText::tr("Enter a size: a number of millimetres (300), a share (1*) or an expression."));
    if (t == QLatin1String("*"))
        return SectionSize::weight(1.0);
    if (t.endsWith(QLatin1Char('*'))) {
        if (const auto w = parsePlainNumber(t.chopped(1)))
            return SectionSize::weight(*w);
        return inputError(
            InteriorText::tr("“%1” is not a share: write a number followed by * (e.g. 2*).").arg(t));
    }
    if (const auto mm = parsePlainNumber(t))
        return SectionSize::fixed(*mm);
    return SectionSize::expr(t.toStdString());
}

core::Result<std::vector<SectionSize>> parseSizeList(const QString& text) {
    std::vector<SectionSize> sizes;
    const QStringList parts = text.split(QLatin1Char(';'));
    for (int i = 0; i < parts.size(); ++i) {
        if (parts[i].trimmed().isEmpty())
            return inputError(
                InteriorText::tr("Entry %1 of the list is empty; separate the sizes with “;”.").arg(i + 1));
        auto size = parseSectionSize(parts[i]);
        if (!size)
            return size.error();
        sizes.push_back(size.value());
    }
    return sizes;
}

QString sizeListText(const std::vector<SectionSize>& sizes) {
    QStringList parts;
    for (const auto& s : sizes)
        parts << sizeText(s);
    return parts.join(QStringLiteral("; "));
}

project::Command splitCommand(const std::string& itemId, const std::string& path, SplitDirection direction,
                              int parts) {
    const std::vector<SectionSize> sizes(static_cast<std::size_t>(std::max(parts, 0)),
                                         SectionSize::weight(1.0));
    return project::commands::splitSection(itemId, path, direction, sizes);
}

core::Result<project::Command> removeSplitCommand(const furniture::CabinetDefinition& cabinet,
                                                  const std::string& itemId, const std::string& path) {
    auto section = furniture::findSection(cabinet.interior, path);
    if (!section)
        return section.error();
    const furniture::Section& current = *section.value();
    if (current.split == SplitDirection::None)
        return inputError(InteriorText::tr("Section “%1” is not split.").arg(QString::fromStdString(path)));
    furniture::Section leaf;
    leaf.size = current.size;
    leaf.front = current.front;
    return project::commands::replaceSection(itemId, path, leaf);
}

core::Result<project::Command> sectionSizeCommand(const std::string& itemId, const std::string& path,
                                                  const QString& text) {
    auto size = parseSectionSize(text);
    if (!size)
        return size.error();
    return project::commands::setSectionSize(itemId, path, size.value());
}

project::Command clearInteriorCommand(const std::string& itemId) {
    return project::commands::setInterior(itemId, furniture::Section{});
}

FrontInput frontInput(const std::optional<furniture::FrontSpec>& front) {
    FrontInput in;
    if (!front)
        return in;
    switch (front->kind) {
    case furniture::FrontKind::Door:
        in.choice = FrontInput::Choice::Door;
        break;
    case furniture::FrontKind::DoorPair:
        in.choice = FrontInput::Choice::DoorPair;
        break;
    case furniture::FrontKind::DrawerStack:
        in.choice = FrontInput::Choice::DrawerStack;
        break;
    case furniture::FrontKind::Flap:
        in.choice = FrontInput::Choice::Flap;
        break;
    case furniture::FrontKind::SlidingDoors:
        in.choice = FrontInput::Choice::SlidingDoors;
        break;
    case furniture::FrontKind::ApplianceNiche:
        in.choice = FrontInput::Choice::ApplianceNiche;
        break;
    case furniture::FrontKind::ApplianceFront:
        in.choice = FrontInput::Choice::ApplianceFront;
        break;
    }
    in.niche = front->niche;
    in.slidingDoors = front->sliding.countExpression.empty()
                          ? QString::number(front->sliding.count)
                          : QString::fromStdString(front->sliding.countExpression);
    in.overlay = front->overlay;
    in.hingeSide = front->hingeSide;
    in.hinges = front->hinges;
    in.handle = front->handle;
    if (!front->drawerHeights.empty())
        in.drawerHeights = sizeListText(front->drawerHeights);
    if (front->drawerBox) {
        in.drawerBox = true;
        in.slide = front->drawerBox->slide.str();
        in.drawerBottom = front->drawerBox->bottom;
    }
    in.style = front->style;
    in.frame = front->frame;
    return in;
}

bool frontStyleApplies(FrontInput::Choice choice) noexcept {
    return choice == FrontInput::Choice::Door || choice == FrontInput::Choice::DoorPair ||
           choice == FrontInput::Choice::Flap || choice == FrontInput::Choice::DrawerStack;
}

std::vector<PanelMaterialChoice> panelMaterialChoices(const furniture::Libraries& libraries,
                                                      const std::string& current) {
    std::vector<PanelMaterialChoice> out;
    out.push_back({std::string{}, InteriorText::tr("Same as front"), false});
    bool found = current.empty();
    for (const auto& m : libraries.materials) {
        out.push_back({m.id.str(), materialChoiceText(m), false});
        found = found || m.id.str() == current;
    }
    if (!found)
        out.push_back({current, InteriorText::tr("%1 (not in the library)").arg(QString::fromStdString(current)),
                       true});
    return out;
}

domain::MaterialId framePanelMaterialId(const furniture::CabinetDefinition& cabinet,
                                        const furniture::FramePanelSpec& frame) {
    if (frame.panelMaterial && !frame.panelMaterial->str().empty()) return *frame.panelMaterial;
    return cabinet.materials.front.str().empty() ? cabinet.materials.carcass : cabinet.materials.front;
}

QString glassPanelMaterialWarning(const furniture::CabinetDefinition& cabinet, const furniture::Libraries& libraries,
                                  const furniture::FramePanelSpec& frame) {
    if (frame.panelKind != furniture::FramePanelKind::Glass) return {};
    const domain::MaterialId id = framePanelMaterialId(cabinet, frame);
    const domain::Material* m = libraries.findMaterial(id);
    if (!m)
        return InteriorText::tr("The glass panel material “%1” is not in the library.").arg(QString::fromStdString(id.str()));
    if (m->kind == domain::MaterialKind::Glass) return {};
    return InteriorText::tr("The glass panel uses “%1”, which is not a glass material; add a glass material "
                            "in the Libraries window and choose it as panel material.")
        .arg(QString::fromStdString(m->name));
}

std::vector<SlideChoice> slideChoices(const furniture::Libraries& libraries, const std::string& current) {
    std::vector<SlideChoice> out;
    bool found = false;
    for (const auto& h : libraries.hardware) {
        if (h.kind != domain::HardwareKind::DrawerSlide)
            continue;
        out.push_back({h.id.str(), h.name, false});
        found = found || h.id.str() == current;
    }
    if (!found && !current.empty())
        out.push_back({current, current, true});
    return out;
}

core::Result<furniture::FrontSpec> frontSpec(const FrontInput& input) {

    if (input.choice == FrontInput::Choice::None)
        return core::makeError(core::ErrorCode::InvalidArgument, "No front is selected.");
    furniture::FrontSpec spec;
    spec.overlay = input.overlay;
    spec.hingeSide = input.hingeSide;
    spec.hinges = input.hinges;
    spec.handle = input.handle;
    switch (input.choice) {
    case FrontInput::Choice::Door:
        spec.kind = furniture::FrontKind::Door;
        break;
    case FrontInput::Choice::DoorPair:
        spec.kind = furniture::FrontKind::DoorPair;
        break;
    case FrontInput::Choice::Flap:
        spec.kind = furniture::FrontKind::Flap;
        break;
    case FrontInput::Choice::SlidingDoors: {
        spec.kind = furniture::FrontKind::SlidingDoors;
        const QString text = input.slidingDoors.trimmed();
        if (text.isEmpty())
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "Enter the number of sliding doors (2 to 6) or an expression.");
        bool isNumber = false;
        const int n = text.toInt(&isNumber);
        if (isNumber) {
            if (n < furniture::kMinSlidingDoors || n > furniture::kMaxSlidingDoors)
                return core::makeError(core::ErrorCode::InvalidArgument,
                                       "A sliding front has 2 to 6 doors; " + std::to_string(n) + " was entered.");
            spec.sliding.count = n;
        } else {
            spec.sliding.countExpression = text.toStdString();
        }
        break;
    }
    case FrontInput::Choice::DrawerStack: {
        spec.kind = furniture::FrontKind::DrawerStack;
        auto heights = parseSizeList(input.drawerHeights);
        if (!heights)
            return heights.error();
        spec.drawerHeights = std::move(heights).value();
        if (input.drawerBox)
            spec.drawerBox = furniture::DrawerBoxSpec{domain::HardwareId(input.slide), input.drawerBottom};
        break;
    }
    case FrontInput::Choice::ApplianceNiche:
        spec.kind = furniture::FrontKind::ApplianceNiche;
        spec.niche = input.niche;
        break;
    case FrontInput::Choice::ApplianceFront:
        spec.kind = furniture::FrontKind::ApplianceFront;
        spec.hinges = false; // fixed to the appliance door
        break;
    case FrontInput::Choice::None:
        break;
    }
    if (frontStyleApplies(input.choice) && input.style == furniture::FrontStyle::FramePanel) {
        spec.style = furniture::FrontStyle::FramePanel;
        spec.frame = input.frame;
    }
    return spec;
}

core::Result<project::Command> frontCommand(const std::string& itemId, const std::string& path,
                                            const FrontInput& input) {
    if (input.choice == FrontInput::Choice::None)
        return project::commands::setFront(itemId, path, std::nullopt);
    auto spec = frontSpec(input);
    if (!spec)
        return spec.error();
    return project::commands::setFront(itemId, path, std::move(spec).value());
}

HangingRailInput hangingRailInput(const std::optional<furniture::HangingRailSpec>& rail) {
    HangingRailInput in;
    if (!rail)
        return in;
    in.enabled = true;
    in.heightFromTop = rail->heightFromTop;
    in.rail = rail->rail.str();
    in.supports = rail->supports.str();
    return in;
}

std::optional<furniture::HangingRailSpec> hangingRailSpec(const HangingRailInput& input) {
    if (!input.enabled)
        return std::nullopt;
    furniture::HangingRailSpec spec;
    spec.heightFromTop = input.heightFromTop;
    spec.rail = domain::HardwareId(input.rail);
    spec.supports = domain::HardwareId(input.supports);
    return spec;
}

namespace {
template <class Accept>
std::vector<HardwareChoice> otherHardwareChoices(const furniture::Libraries& libraries, const std::string& current,
                                                 Accept accept) {
    std::vector<HardwareChoice> out;
    bool found = false;
    for (const auto& h : libraries.hardware) {
        if (h.kind != domain::HardwareKind::Other || !accept(h))
            continue;
        out.push_back({h.id.str(), h.name, false, false});
        found = found || h.id.str() == current;
    }
    if (!found && !current.empty())
        out.push_back({current, current, true, false});
    return out;
}
} // namespace

std::vector<HardwareChoice> clothesRailChoices(const furniture::Libraries& libraries, const std::string& current) {
    return otherHardwareChoices(libraries, current, [](const domain::HardwareDefinition& h) {
        return h.params.count(hardware::keys::kRailHeight) && h.params.count(hardware::keys::kHookClearance);
    });
}

std::vector<HardwareChoice> railSupportChoices(const furniture::Libraries& libraries, const std::string& current) {
    return otherHardwareChoices(libraries, current, [](const domain::HardwareDefinition& h) {
        return h.params.count(hardware::keys::kBodyHeight) && h.params.count(hardware::keys::kScrewSpacing) &&
               !h.params.count(hardware::keys::kRailHeight);
    });
}

core::Result<project::Command> hangingRailCommand(const furniture::CabinetDefinition& cabinet,
                                                  const std::string& itemId, const std::string& path,
                                                  const HangingRailInput& input) {
    auto section = furniture::findSection(cabinet.interior, path);
    if (!section)
        return section.error();
    const furniture::Section& current = *section.value();
    if (current.split != SplitDirection::None)
        return inputError(InteriorText::tr("Section “%1” is split: a clothes rail goes into a section that is not "
                                           "split.")
                              .arg(path.empty() ? InteriorText::tr("whole interior") : QString::fromStdString(path)));
    furniture::Section leaf = current;
    leaf.hangingRail = hangingRailSpec(input);
    return project::commands::replaceSection(itemId, path, std::move(leaf));
}

} // namespace fos::app
