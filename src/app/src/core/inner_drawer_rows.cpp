// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/inner_drawer_rows.hpp"

#include "fos/app/interior_rows.hpp"

#include <QCoreApplication>

namespace fos::app {

namespace {
class InnerDrawerText {
    Q_DECLARE_TR_FUNCTIONS(InnerDrawerText)
};

core::Error inputError(const QString& message) {
    return core::makeError(core::ErrorCode::InvalidArgument, message.toStdString());
}

constexpr std::string_view kCodePrefix = "furniture.inner_drawers.";
} // namespace

InnerDrawerInput innerDrawerInput(const std::optional<furniture::InnerDrawerSpec>& spec) {
    InnerDrawerInput in;
    if (!spec)
        return in;
    in.enabled = true;
    in.heights = sizeListText(spec->heights);
    in.slide = spec->slide.str();
    in.bottom = spec->bottom;
    in.joint = spec->joint;
    in.front = spec->front;
    in.handle = spec->handle;
    return in;
}

core::Result<std::optional<furniture::InnerDrawerSpec>> innerDrawerSpec(const InnerDrawerInput& input) {
    if (!input.enabled)
        return std::optional<furniture::InnerDrawerSpec>{};
    auto heights = parseSizeList(input.heights);
    if (!heights)
        return heights.error();
    furniture::InnerDrawerSpec spec;
    spec.heights = std::move(heights).value();
    spec.slide = domain::HardwareId(input.slide);
    spec.bottom = input.bottom;
    spec.joint = input.joint;
    spec.front = input.front;
    spec.handle = input.handle;
    return std::optional<furniture::InnerDrawerSpec>(std::move(spec));
}

core::Result<project::Command> innerDrawersCommand(const furniture::CabinetDefinition& cabinet,
                                                   const std::string& itemId, const std::string& path,
                                                   const InnerDrawerInput& input) {
    auto section = furniture::findSection(cabinet.interior, path);
    if (!section)
        return section.error();
    const furniture::Section& current = *section.value();
    if (current.split != furniture::SplitDirection::None)
        return inputError(
            InnerDrawerText::tr("Section “%1” is split: inner drawers go into a section that is not split.")
                .arg(path.empty() ? InnerDrawerText::tr("whole interior") : QString::fromStdString(path)));
    auto spec = innerDrawerSpec(input);
    if (!spec)
        return spec.error();
    furniture::Section leaf = current;
    leaf.innerDrawers = std::move(spec).value();
    return project::commands::replaceSection(itemId, path, std::move(leaf));
}

std::optional<furniture::FrontKind> coveringFront(const furniture::Section& root, const std::string& path) {
    if (!furniture::findSection(root, path))
        return std::nullopt;
    // Walk from the root down the path; the deepest front seen covers the section.
    std::optional<furniture::FrontKind> front;
    const furniture::Section* s = &root;
    if (s->front)
        front = s->front->kind;
    std::size_t start = 0;
    while (!path.empty() && start <= path.size()) {
        const auto dot = path.find('.', start);
        const std::string prefix = path.substr(0, dot);
        auto child = furniture::findSection(root, prefix);
        if (!child)
            return std::nullopt;
        s = child.value();
        if (s->front)
            front = s->front->kind;
        if (dot == std::string::npos)
            break;
        start = dot + 1;
    }
    return front;
}

bool innerDrawersEditable(const furniture::Section& root, const std::string& path) {
    auto section = furniture::findSection(root, path);
    if (!section || section.value()->split != furniture::SplitDirection::None)
        return false;
    if (section.value()->innerDrawers)
        return true;
    const auto front = coveringFront(root, path);
    return front && (*front == furniture::FrontKind::Door || *front == furniture::FrontKind::DoorPair ||
                     *front == furniture::FrontKind::SlidingDoors);
}

QString innerDrawersText(const furniture::InnerDrawerSpec& spec) {
    const int n = static_cast<int>(spec.heights.size());
    return spec.front == furniture::InnerDrawerFront::Facia
               ? InnerDrawerText::tr("inner drawers: %1 with facias").arg(n)
               : InnerDrawerText::tr("inner drawers: %1").arg(n);
}

core::Diagnostics innerDrawerFindings(const project::Resolved& resolved, const std::string& itemId) {
    core::Diagnostics out;
    for (const auto& item : resolved.items) {
        if (item.itemId != itemId)
            continue;
        for (const auto& d : item.diagnostics)
            if (d.severity != core::Severity::Info && d.code.compare(0, kCodePrefix.size(), kCodePrefix) == 0)
                out.push_back(d);
    }
    return out;
}

} // namespace fos::app
