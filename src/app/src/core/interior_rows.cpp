// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/interior_rows.hpp"

#include "fos/app/parameter_rows.hpp"
#include "fos/core/units.hpp"

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
    else
        text = InteriorText::tr("empty");
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
        }
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
    }
    in.overlay = front->overlay;
    in.hingeSide = front->hingeSide;
    in.hinges = front->hinges;
    in.handle = front->handle;
    if (!front->drawerHeights.empty())
        in.drawerHeights = sizeListText(front->drawerHeights);
    return in;
}

core::Result<project::Command> frontCommand(const std::string& itemId, const std::string& path,
                                            const FrontInput& input) {
    if (input.choice == FrontInput::Choice::None)
        return project::commands::setFront(itemId, path, std::nullopt);
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
    case FrontInput::Choice::DrawerStack: {
        spec.kind = furniture::FrontKind::DrawerStack;
        auto heights = parseSizeList(input.drawerHeights);
        if (!heights)
            return heights.error();
        spec.drawerHeights = std::move(heights).value();
        break;
    }
    case FrontInput::Choice::None:
        break;
    }
    return project::commands::setFront(itemId, path, spec);
}

} // namespace fos::app
