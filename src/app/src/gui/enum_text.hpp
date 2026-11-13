// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Translated display texts of engineering enumerations (the core keeps stable English tokens).
#include "fos/app/interior_rows.hpp"
#include "fos/core/diagnostic.hpp"
#include "fos/cutting/types.hpp"
#include "fos/drawings/annotation.hpp"
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
/// Front editor and cutting / drawing editor choices.
Choices<FrontInput::Choice> frontChoices();
Choices<furniture::OverlayKind> overlayChoices();
Choices<furniture::HingeSide> hingeSideChoices();
Choices<furniture::HandlePosition> handleChoices();
Choices<cutting::Objective> objectiveChoices();
Choices<drawings::DimensionOrientation> orientationChoices();

} // namespace fos::app::gui
