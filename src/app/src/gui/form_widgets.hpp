// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Small helpers for the editor forms (combo boxes filled from translated enum choices).
#include "enum_text.hpp"

#include <QComboBox>

#include <algorithm>

namespace fos::app::gui {

/// Combo box that does not force its form wider than the dock (long translated texts).
inline QComboBox* narrowCombo(QWidget* parent, int minimumCharacters = 12) {
    auto* box = new QComboBox(parent);
    box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    box->setMinimumContentsLength(minimumCharacters);
    return box;
}

/// Combo box of (text, enum) choices with `current` selected; the item data is the enum value.
template <class E> QComboBox* choiceBox(const Choices<E>& choices, E current, QWidget* parent) {
    auto* box = narrowCombo(parent);
    for (const auto& [text, value] : choices) {
        box->addItem(text, static_cast<int>(value));
        if (value == current)
            box->setCurrentIndex(box->count() - 1);
    }
    return box;
}

template <class E> E valueOf(const QComboBox* box) {
    return static_cast<E>(box->currentData().toInt());
}

/// Selects the entry whose data equals `data` (the first entry when there is none).
inline void selectData(QComboBox* box, const QVariant& data) {
    box->setCurrentIndex(std::max(0, box->findData(data)));
}

} // namespace fos::app::gui
