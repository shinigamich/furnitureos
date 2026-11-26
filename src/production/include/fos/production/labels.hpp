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
    /// Contoured part (ADR-0008): `edgeCode` is then "0000" (Part::edges is empty) and the
    /// outline bands are in `outlineEdgeCode`, one character per outline segment ('1', '0',
    /// '?'). The SVG label prints LabelText::shaped and that code instead of the rectangle.
    bool shaped = false;
    std::string outlineEdgeCode;
    int indexInGroup = 0;  ///< 1-based position inside its cut-list group.
    int groupQuantity = 0; ///< Size of that group.

    std::string quantityText() const; ///< e.g. "2/5".
};

struct LabelSet {
    std::vector<Label> labels; ///< Part-number order.
    core::Diagnostics diagnostics;
};

/// One label per cut-list part; bought-in parts (domain::isBoughtIn) get none (one Info
/// production.bought_in.excluded).
LabelSet labels(const domain::Model& model, const LabelOptions& options = {}, const Scope& scope = {});

/// Captions printed on a label (English defaults; pass translations from the app).
struct LabelText {
    std::string cut = "Cut";
    std::string number = "No.";
    std::string shaped = "Shaped";
};

/// Machine-readable code printed on each part label.
enum class LabelBarcode {
    None,    ///< No barcode (default; output identical to label sheets without barcode support).
    Code128, ///< Code 128, code set B, with checksum (see fos/production/code128.hpp).
};

/// What the label barcode encodes.
enum class LabelBarcodeContent {
    PartId,           ///< `Label::partId`, e.g. "cab1/side.left".
    ProjectAndPartId, ///< "<Label::projectName>|<part id>", e.g. "Kitchen|cab1/side.left".
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
    /// Optional barcode. When drawn it replaces the part-id text line in a band across the label
    /// (design rows 26.0..31.6 of the 37 mm design, scaled with the label height), with
    /// `kCode128QuietZoneModules` blank modules on each side inside the 2 mm (scaled) padding.
    /// The module width is the band width divided by the module count (no maximum); when that is
    /// below `barcodeMinModule` the barcode is omitted for that label (the part-id text stays) and
    /// a warning `production.label.barcode_too_narrow` is emitted instead of shrinking it.
    LabelBarcode barcode = LabelBarcode::None;
    LabelBarcodeContent barcodeContent = LabelBarcodeContent::PartId;
    double barcodeMinModule = 0.25; ///< mm on paper; must be positive (0.25 mm = 2 dots at 203 dpi).
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
/// Barcode warnings are dropped here; use `renderLabelSheets` when `options.barcode` is set.
core::Result<std::vector<std::string>> renderLabelSheetsSvg(const std::vector<Label>& labels,
                                                            const LabelSheetOptions& options = {});

/// Label sheet pages plus rendering diagnostics (barcode omitted: too narrow / unencodable text).
struct LabelSheets {
    std::vector<std::string> pages;
    core::Diagnostics diagnostics; ///< In label order.
};

/// As `renderLabelSheetsSvg`, also reporting why a barcode was omitted. Error (InvalidArgument)
/// for an invalid layout or a non-positive / non-finite `barcodeMinModule` with a barcode set.
core::Result<LabelSheets> renderLabelSheets(const std::vector<Label>& labels, const LabelSheetOptions& options = {});

/// The text a label barcode encodes for `content` (see `LabelBarcodeContent`).
std::string labelBarcodeText(const Label& label, LabelBarcodeContent content);

} // namespace fos::production
