// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Drawings workspace view-model: which drawing with which sheet options, and file export.
/// Drawing geometry comes from project::assemblyDrawing / partDrawing (drawings module).
#include "fos/core/result.hpp"
#include "fos/project/outputs.hpp"

#include <QCoreApplication>
#include <QString>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fos::app {

struct DrawingRequest {
    enum class Kind { Assembly, Part };
    Kind kind = Kind::Assembly;
    std::string id; ///< Item id (Assembly) or part id (Part).
    drawings::PaperFormat format = drawings::PaperFormat::A3;
    drawings::PaperOrientation orientation = drawings::PaperOrientation::Landscape;
    /// nullopt = largest standard scale that fits (fit to page); otherwise exactly this scale.
    std::optional<drawings::Scale> scale;
    std::string date; ///< Title-block date (ISO yyyy-mm-dd), supplied by the caller.
};

class DrawingText {
    Q_DECLARE_TR_FUNCTIONS(DrawingText)
public:
    /// Title-block captions in the UI language.
    static drawings::TitleBlockLabels titleLabels();
    /// Calibration-mark unit and note lines in the UI language.
    static drawings::CalibrationOptions calibration();
};

/// Generates the requested drawing with translated title block and calibration note; title
/// data: project name and author from the metadata, title = item / part name.
core::Result<drawings::DrawingResult> generateDrawing(const project::Project& project,
                                                      const project::Resolved& resolved,
                                                      const DrawingRequest& request);

/// Files written by an export.
using WrittenFiles = std::vector<std::filesystem::path>;

/// One SVG per sheet (drawings::toSvg, white page): `path` for a single sheet, otherwise
/// "<stem>-<n><ext>" (n = 1, 2, ...).
core::Result<WrittenFiles> exportDrawingSvg(const drawings::DrawingResult& drawing, const std::filesystem::path& path);
/// One DXF per generated view (drawings::viewToDxf: model space 1:1, mm; annotation sizes
/// multiplied by the inverse of the sheet scale so they keep their paper size relative to
/// the geometry): `path` for a single view, otherwise "<stem>-<view index><ext>".
core::Result<WrittenFiles> exportDrawingDxf(const drawings::DrawingResult& drawing, const std::filesystem::path& path);
/// All sheets as a multi-page vector PDF at exact scale (printing::exportPdf).
core::Result<void> exportDrawingPdf(const drawings::DrawingResult& drawing, const std::filesystem::path& path,
                                    const std::string& title);

} // namespace fos::app
