// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Translated captions handed to the production module (its tables, HTML sheet and labels
/// take all captions as parameters; no language is hard-coded there).
#include "fos/production/html.hpp"
#include "fos/production/labels.hpp"
#include "fos/production/tables.hpp"

#include <QCoreApplication>

namespace fos::app {

class ProductionText {
    Q_DECLARE_TR_FUNCTIONS(ProductionText)
public:
    /// Every header and in-cell term in the current UI language (English without translator).
    static production::Localisation localisation();
    static production::HtmlSheetText htmlSheetText();
    static production::LabelText labelText();
};

} // namespace fos::app
