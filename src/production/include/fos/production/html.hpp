// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/production/tables.hpp"

#include <string_view>

namespace fos::production {

/// Escapes & < > " ' for XML/HTML text and attribute values. UTF-8 bytes pass
/// through unchanged; control characters that XML 1.0 forbids (below U+0020 except
/// tab, LF, CR) are replaced by a space. Input is assumed to be valid UTF-8.
std::string escapeXml(std::string_view text);

/// Renders a table as an HTML `<table>` fragment (all cells escaped).
std::string tableHtml(const Table& table);

struct HtmlSheetText {
    std::string lang = "en";                 ///< Value of <html lang>.
    std::string title = "Production sheet";
    std::string project = "Project";
    std::string assembly = "Assembly";
    std::string envelope = "Envelope (W x D x H, mm)";
    std::string partList = "Part list";
    std::string cutList = "Cut list";
    std::string edgeBanding = "Edge banding";
    std::string hardware = "Hardware";
    std::string warnings = "Warnings";
    std::string none = "none";               ///< Shown for an empty section.
};

struct HtmlSheetOptions {
    std::string projectName;
    HtmlSheetText text;
    Localisation localisation;
    NumberFormat numbers;
    EdgeBandingOptions edgeBanding;
    CutListOptions cutList;
};

/// Self-contained, printable (A4, print CSS, no external resources or scripts) UTF-8
/// HTML document for ONE assembly: part list, cut list, edge banding and hardware,
/// followed by the diagnostics of those reports (English messages from the core).
/// NotFound when the assembly id is not in the model.
core::Result<std::string> productionSheetHtml(const domain::Model& model, const domain::AssemblyId& assembly,
                                              const HtmlSheetOptions& options = {});

} // namespace fos::production
