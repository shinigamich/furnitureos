// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/core/result.hpp"
#include "fos/production/cut_list.hpp"

namespace fos::production {

struct LabelOptions {
    std::string projectName;    ///< `domain::Model` carries no project name; supplied by the caller.
    CutListOptions cutList;     ///< Grouping used for the "2/5" quantity index.
};

/// One label per part instance.
struct Label {
    std::string projectName, assemblyName, partName;
    domain::PartId partId;
    int partNumber = 0; ///< PartRow::number (unique in the model, stable for an unchanged model).
    double length = 0, width = 0, thickness = 0; ///< Finished, mm.
    double cutLength = 0, cutWidth = 0;          ///< mm.
    std::string materialName;   ///< Material name; the raw material id when the material is missing.
    bool materialMissing = false;
    /// Edge-banding diagram code: four characters for L1, L2, W1, W2 in that order;
    /// '1' = banded, '0' = raw, '?' = references an edge band missing from the model.
    std::string edgeCode;
    std::array<double, 4> edgeThickness{}; ///< mm per side (0 = raw or missing), for diagrams.
    int indexInGroup = 0;  ///< 1-based position inside its cut-list group.
    int groupQuantity = 0; ///< Size of that group.

    std::string quantityText() const; ///< e.g. "2/5".
};

struct LabelSet {
    std::vector<Label> labels; ///< Part-number order.
    core::Diagnostics diagnostics;
};

LabelSet labels(const domain::Model& model, const LabelOptions& options = {}, const Scope& scope = {});

/// Captions printed on a label (English defaults; pass translations from the app).
struct LabelText {
    std::string cut = "Cut";
    std::string number = "No.";
};

/// Label sheet layout. All values in mm on paper. Labels are placed row by row from
/// the top-left corner. Defaults: A4 portrait, 70 x 37 labels, 3 x 8 per page.
struct LabelSheetOptions {
    double pageWidth = 210.0, pageHeight = 297.0;
    double labelWidth = 70.0, labelHeight = 37.0;
    double marginLeft = 0.0, marginTop = 0.5;
    double gapX = 0.0, gapY = 0.0;
    bool drawBorders = true;  ///< Thin cutting/alignment outline around each label.
    int decimals = core::kDisplayDecimals;
    LabelText text;
};

struct LabelGrid {
    int columns = 0, rows = 0;
    int perPage() const noexcept { return columns * rows; }
};

/// Number of whole labels that fit on a page. Error (InvalidArgument) when the sizes
/// are not positive/finite or not even one label fits.
core::Result<LabelGrid> labelGrid(const LabelSheetOptions& options);

/// Renders the labels as standalone UTF-8 SVG documents, one per page, in exact
/// millimetre units (`width="210mm" height="297mm" viewBox="0 0 210 297"` for A4, so
/// one user unit is 1 mm). All text is XML-escaped; text of each label is clipped to
/// its own cell (no font metrics are available here, so long names are cut off at
/// the label edge rather than shrunk or wrapped). No labels -> no pages.
core::Result<std::vector<std::string>> renderLabelSheetsSvg(const std::vector<Label>& labels,
                                                            const LabelSheetOptions& options = {});

} // namespace fos::production
