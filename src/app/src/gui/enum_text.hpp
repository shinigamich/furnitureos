// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Translated display texts of engineering enumerations (the core keeps stable English tokens).
#include "fos/core/diagnostic.hpp"
#include "fos/domain/part.hpp"
#include "fos/furniture/enums.hpp"

#include <QString>

#include <utility>
#include <vector>

namespace fos::app::gui {

QString roleText(domain::PartRole role);
QString grainText(domain::GrainDirection grain);
QString severityText(core::Severity severity);

/// (display text, enum value) pairs for the construction option combo boxes.
template <class E> using Choices = std::vector<std::pair<QString, E>>;
Choices<furniture::JointKind> jointChoices();
Choices<furniture::TopKind> topChoices();
Choices<furniture::BackMount> backChoices();
Choices<furniture::PlinthKind> plinthChoices();
Choices<furniture::JointFastener> fastenerChoices();

} // namespace fos::app::gui
