// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// The furniture templates offered by "New from template" (furniture/templates.hpp).
#include "fos/project/project.hpp"

#include <QString>

#include <optional>
#include <vector>

namespace fos::app {

enum class TemplateKind { BaseCabinet, WallCabinet, TallCabinet, DrawerUnit, WardrobeSection, OpenShelving };

/// All kinds in menu order.
const std::vector<TemplateKind>& allTemplates();
/// Translated menu text, e.g. "Base cabinet".
QString templateTitle(TemplateKind kind);
/// Stable token used in tests and settings, e.g. "base".
const char* templateToken(TemplateKind kind) noexcept;

/// Overall size of a cabinet in mm (the standard parameters width / height / depth).
struct TemplateSize {
    double width = 0, height = 0, depth = 0;
};

/// The template's own default size, read from the parameter graph of the generated
/// definition (not duplicated here).
TemplateSize defaultTemplateSize(TemplateKind kind);

/// A cabinet item from the template with the generic default materials
/// (project::defaultTemplateMaterials()); width / height / depth are set through
/// furniture::setParameter against `libraries`, so an impossible size is refused with the
/// furniture module's message. `name` empty = the template's own name.
core::Result<project::Item> makeTemplateItem(TemplateKind kind, const std::string& id, const std::string& name,
                                             const furniture::Libraries& libraries,
                                             const std::optional<TemplateSize>& size = std::nullopt);

/// First unused item id "<prefix><n>" (n = 1, 2, ...).
std::string nextItemId(const project::Project& project, const std::string& prefix = "cab");
/// `base` when no item has that name, otherwise "base 2", "base 3", ...
std::string uniqueItemName(const project::Project& project, const std::string& base);

} // namespace fos::app
