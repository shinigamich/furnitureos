// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Drawings workspace: assembly / part drawing sheets (drawings module) previewed with
/// printing::renderPreview, sheet/scale/paper options, zoom, export and exact-scale printing.
#include "fos/app/drawing_request.hpp"

#include <QImage>
#include <QWidget>

#include <optional>

class QComboBox;
class QLabel;
class QScrollArea;
class QSplitter;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

class AnnotationPanel;
class SheetPreview;

class DrawingsWorkspace : public QWidget {
    Q_OBJECT
public:
    explicit DrawingsWorkspace(Session& session, QWidget* parent = nullptr);

    /// The drawing currently shown (nullptr when it could not be generated).
    const drawings::DrawingResult* drawing() const noexcept { return drawing_ ? &*drawing_ : nullptr; }
    /// Raster preview of the current sheet (null image when none).
    const QImage& preview() const noexcept { return preview_; }
    /// Name used for file names and the PDF title.
    QString drawingName() const;
    /// Annotation editor next to the preview.
    AnnotationPanel* annotations() const noexcept { return annotations_; }
    /// Index of the sheet shown.
    int sheetIndex() const;
    /// Same as a left click on the preview at paper point (x, y) mm of the current sheet.
    void clickPaper(double x, double y);

    void exportPdf();
    void exportSvg();
    void exportDxf();
    void print();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void refillTargets();
    void regenerate();
    void renderSheet();
    void showOverlay();
    void previewClicked(QPointF pixel);
    std::optional<DrawingRequest> request() const;
    bool ensureDrawing();

    Session& session_;
    QComboBox* target_;
    QComboBox* paper_;
    QComboBox* orientation_;
    QComboBox* scale_;
    QComboBox* sheet_;
    QComboBox* zoom_;
    QLabel* status_;
    SheetPreview* image_;
    QScrollArea* scroll_;
    QSplitter* splitter_;
    AnnotationPanel* annotations_;
    double pxPerMm_ = 0;
    std::optional<drawings::DrawingResult> drawing_;
    QImage preview_;
    bool stale_ = true;
};

} // namespace fos::app::gui
