// SPDX-License-Identifier: AGPL-3.0-only
#include "drawings_workspace.hpp"

#include "drawing_annotations.hpp"

#include "fos/app/session.hpp"
#include "fos/printing/paint.hpp"
#include "fos/printing/print.hpp"

#include <QComboBox>
#include <QDate>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>

namespace fos::app::gui {
namespace {
constexpr const char* kAssemblyPrefix = "A:";
constexpr const char* kPartPrefix = "P:";

QString filesText(const WrittenFiles& files) {
    QStringList names;
    for (const auto& f : files) names << QString::fromStdString(f.string());
    return names.join(QLatin1Char('\n'));
}
} // namespace

DrawingsWorkspace::DrawingsWorkspace(Session& session, QWidget* parent) : QWidget(parent), session_(session) {
    setObjectName(QStringLiteral("drawingsWorkspace"));
    target_ = new QComboBox(this);
    target_->setMinimumContentsLength(26);
    target_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    paper_ = new QComboBox(this);
    for (auto [text, f] : {std::pair{"A4", drawings::PaperFormat::A4}, std::pair{"A3", drawings::PaperFormat::A3},
                           std::pair{"A2", drawings::PaperFormat::A2}, std::pair{"A1", drawings::PaperFormat::A1}})
        paper_->addItem(QString::fromLatin1(text), static_cast<int>(f));
    paper_->setCurrentIndex(1);
    orientation_ = new QComboBox(this);
    orientation_->addItem(tr("Landscape"), static_cast<int>(drawings::PaperOrientation::Landscape));
    orientation_->addItem(tr("Portrait"), static_cast<int>(drawings::PaperOrientation::Portrait));
    scale_ = new QComboBox(this);
    scale_->addItem(tr("Fit to sheet"), 0.0);
    for (double d : {1.0, 2.0, 5.0, 10.0, 20.0, 50.0}) scale_->addItem(QStringLiteral("1:%1").arg(d), d);
    sheet_ = new QComboBox(this);
    zoom_ = new QComboBox(this);
    zoom_->addItem(tr("Fit width"), 0);
    for (int z : {25, 50, 75, 100, 150, 200, 400}) zoom_->addItem(QStringLiteral("%1 %").arg(z), z);
    zoom_->setCurrentIndex(0);
    zoom_->setToolTip(tr("100 % = real paper size on a correctly calibrated screen"));

    // Two rows so that long (translated) captions never force the window wider.
    auto* bar = new QHBoxLayout;
    auto* bar2 = new QHBoxLayout;
    auto addLabeled = [&](QHBoxLayout* row, const QString& text, QWidget* w) {
        row->addWidget(new QLabel(text, this));
        row->addWidget(w);
    };
    addLabeled(bar, tr("Drawing:"), target_);
    bar->addStretch(1);
    addLabeled(bar, tr("Paper:"), paper_);
    bar->addWidget(orientation_);
    addLabeled(bar, tr("Scale:"), scale_);
    addLabeled(bar2, tr("Sheet:"), sheet_);
    addLabeled(bar2, tr("Zoom:"), zoom_);
    bar2->addStretch(1);
    auto* pdf = new QPushButton(tr("PDF…"), this);
    auto* svg = new QPushButton(tr("SVG…"), this);
    auto* dxf = new QPushButton(tr("DXF…"), this);
    auto* print = new QPushButton(tr("Print…"), this);
    for (QPushButton* b : {pdf, svg, dxf, print}) bar2->addWidget(b);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    image_ = new SheetPreview(this);
    image_->setObjectName(QStringLiteral("drawingPreview"));
    image_->setAlignment(Qt::AlignCenter);
    scroll_ = new QScrollArea(this);
    scroll_->setWidget(image_);
    scroll_->setWidgetResizable(true);
    scroll_->setBackgroundRole(QPalette::Dark);
    annotations_ = new AnnotationPanel(session_, this);
    auto* panelScroll = new QScrollArea(this);
    panelScroll->setWidget(annotations_);
    panelScroll->setWidgetResizable(true);
    panelScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    panelScroll->setMinimumWidth(280);
    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->addWidget(scroll_);
    splitter_->addWidget(panelScroll);
    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 0);
    splitter_->setChildrenCollapsible(false);
    splitter_->setSizes({640, 290});

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(bar);
    layout->addLayout(bar2);
    layout->addWidget(status_);
    layout->addWidget(splitter_, 1);
    connect(image_, &SheetPreview::clicked, this, &DrawingsWorkspace::previewClicked);
    connect(annotations_, &AnnotationPanel::overlayChanged, this, &DrawingsWorkspace::showOverlay);

    connect(&session_, &Session::projectChanged, this, [this] {
        refillTargets();
        stale_ = true;
        if (isVisible()) regenerate();
    });
    connect(&session_, &Session::selectionChanged, this, [this] {
        // Follow the selection: a selected part shows its part drawing, an item its assembly.
        const Selection& sel = session_.selection();
        refillTargets();
        QString wanted;
        if (sel.partIds.size() == 1) wanted = QString::fromLatin1(kPartPrefix) + QString::fromStdString(sel.partIds[0]);
        else if (!sel.itemId.empty()) wanted = QString::fromLatin1(kAssemblyPrefix) + QString::fromStdString(sel.itemId);
        const int index = target_->findData(wanted);
        if (index >= 0 && index != target_->currentIndex()) {
            target_->setCurrentIndex(index);
        }
        stale_ = true;
        if (isVisible()) regenerate();
    });
    for (QComboBox* c : {target_, paper_, orientation_, scale_})
        connect(c, qOverload<int>(&QComboBox::activated), this, [this] { regenerate(); });
    connect(sheet_, qOverload<int>(&QComboBox::activated), this, [this] { renderSheet(); });
    connect(zoom_, qOverload<int>(&QComboBox::activated), this, [this] { renderSheet(); });
    connect(pdf, &QPushButton::clicked, this, &DrawingsWorkspace::exportPdf);
    connect(svg, &QPushButton::clicked, this, &DrawingsWorkspace::exportSvg);
    connect(dxf, &QPushButton::clicked, this, &DrawingsWorkspace::exportDxf);
    connect(print, &QPushButton::clicked, this, &DrawingsWorkspace::print);
    refillTargets();
}

void DrawingsWorkspace::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (stale_) regenerate();
}

void DrawingsWorkspace::refillTargets() {
    const QString current = target_->currentData().toString();
    target_->blockSignals(true);
    target_->clear();
    if (const project::Resolved* r = session_.resolved()) {
        const std::string selectedItem = session_.selection().itemId;
        for (const auto& a : r->model.assemblies) {
            target_->addItem(tr("%1 — assembly drawing").arg(QString::fromStdString(a.name)),
                             QString::fromLatin1(kAssemblyPrefix) + QString::fromStdString(a.id.str()));
            if (a.id.str() != selectedItem) continue;
            for (const auto& p : a.parts)
                target_->addItem(QStringLiteral("    ") + tr("%1 — part drawing").arg(QString::fromStdString(p.name)),
                                 QString::fromLatin1(kPartPrefix) + QString::fromStdString(p.id.str()));
        }
    }
    target_->setCurrentIndex(std::max(0, target_->findData(current)));
    target_->blockSignals(false);
}

std::optional<DrawingRequest> DrawingsWorkspace::request() const {
    const QString selected = target_->currentData().toString();
    if (selected.isEmpty()) return std::nullopt;
    DrawingRequest r;
    r.kind = selected.startsWith(QLatin1String(kPartPrefix)) ? DrawingRequest::Kind::Part : DrawingRequest::Kind::Assembly;
    r.id = selected.mid(2).toStdString();
    r.format = static_cast<drawings::PaperFormat>(paper_->currentData().toInt());
    r.orientation = static_cast<drawings::PaperOrientation>(orientation_->currentData().toInt());
    const double denominator = scale_->currentData().toDouble();
    if (denominator > 0) r.scale = drawings::Scale{1, denominator};
    r.date = QDate::currentDate().toString(Qt::ISODate).toStdString();
    return r;
}

void DrawingsWorkspace::regenerate() {
    stale_ = false;
    annotations_->setDrawing(nullptr, request(), 0); // the old result is about to be destroyed
    drawing_.reset();
    const auto req = request();
    const project::Resolved* resolved = session_.resolved();
    if (!req || !resolved) {
        status_->setText(tr("Nothing to draw: the project has no generated item."));
        sheet_->clear();
        renderSheet();
        return;
    }
    auto result = generateDrawing(session_.project(), *resolved, *req);
    if (!result) {
        status_->setText(tr("The drawing cannot be generated: %1").arg(QString::fromStdString(result.error().message)));
        status_->setStyleSheet(QStringLiteral("color: #b00020;"));
        sheet_->clear();
        renderSheet();
        return;
    }
    drawing_ = std::move(result).value();
    status_->setStyleSheet({});
    const int keep = sheet_->currentIndex();
    sheet_->clear();
    for (std::size_t i = 0; i < drawing_->sheets.size(); ++i)
        sheet_->addItem(tr("%1 of %2 (%3)")
                            .arg(i + 1)
                            .arg(drawing_->sheets.size())
                            .arg(QString::fromStdString(drawing_->sheets[i].scale.text())));
    sheet_->setCurrentIndex(keep >= 0 && keep < sheet_->count() ? keep : 0);
    QString text = tr("Scale %1. The sheet carries a 100 mm calibration mark for checking printed scale.")
                       .arg(QString::fromStdString(drawing_->sheets.front().scale.text()));
    if (!drawing_->diagnostics.empty())
        text += QLatin1Char(' ') + tr("%1 drawing note(s); first: %2")
                                       .arg(drawing_->diagnostics.size())
                                       .arg(QString::fromStdString(drawing_->diagnostics.front().message));
    status_->setText(text);
    renderSheet();
}

void DrawingsWorkspace::renderSheet() {
    preview_ = QImage();
    pxPerMm_ = 0;
    const int index = sheet_->currentIndex();
    if (drawing_ && index >= 0 && index < static_cast<int>(drawing_->sheets.size())) {
        const auto& sheet = drawing_->sheets[static_cast<std::size_t>(index)];
        const int zoom = zoom_->currentData().toInt();
        const double fit = std::max(1, scroll_->viewport()->width() - 24) / sheet.width;
        pxPerMm_ = zoom > 0 ? zoom / 100.0 * logicalDpiX() / 25.4 : fit;
        preview_ = printing::renderPreview(sheet, pxPerMm_);
    }
    // The panel follows the drawing and sheet shown (its overlay is drawn by showOverlay()).
    std::optional<DrawingRequest> req = drawing_ ? request() : std::nullopt;
    annotations_->setDrawing(drawing_ ? &*drawing_ : nullptr, req, static_cast<std::size_t>(std::max(0, index)));
    showOverlay();
}

int DrawingsWorkspace::sheetIndex() const { return sheet_->currentIndex(); }

void DrawingsWorkspace::showOverlay() {
    if (preview_.isNull() || pxPerMm_ <= 0 || !drawing_) {
        image_->setPixmap(preview_.isNull() ? QPixmap() : QPixmap::fromImage(preview_));
        return;
    }
    const double height = drawing_->sheets[static_cast<std::size_t>(std::max(0, sheet_->currentIndex()))].height;
    const auto toPixel = [&](geometry::Vec2 p) { return QPointF(p.x * pxPerMm_, (height - p.y) * pxPerMm_); };
    QImage shown = preview_;
    QPainter painter(&shown);
    painter.setRenderHint(QPainter::Antialiasing);
    const drawings::Rect2 box = annotations_->highlight();
    if (!box.empty()) {
        const QRectF r = QRectF(toPixel(box.min), toPixel(box.max)).normalized().adjusted(-3, -3, 3, 3);
        painter.setPen(QPen(QColor(0, 102, 204), 1.5));
        painter.setBrush(QColor(0, 102, 204, 40));
        painter.drawRect(r);
    }
    painter.setPen(QPen(QColor(204, 0, 0), 2));
    painter.setBrush(Qt::NoBrush);
    for (const auto& m : annotations_->markers()) painter.drawEllipse(toPixel(m), 5, 5);
    painter.end();
    image_->setPixmap(QPixmap::fromImage(shown));
}

void DrawingsWorkspace::previewClicked(QPointF pixel) {
    const int index = sheet_->currentIndex();
    if (!drawing_ || pxPerMm_ <= 0 || index < 0 || index >= static_cast<int>(drawing_->sheets.size())) return;
    const double height = drawing_->sheets[static_cast<std::size_t>(index)].height;
    annotations_->sheetClicked({pixel.x() / pxPerMm_, height - pixel.y() / pxPerMm_});
}

void DrawingsWorkspace::clickPaper(double x, double y) {
    if (stale_) regenerate();
    if (pxPerMm_ <= 0 || !drawing_) return;
    const double height = drawing_->sheets[static_cast<std::size_t>(std::max(0, sheet_->currentIndex()))].height;
    previewClicked(QPointF(x * pxPerMm_, (height - y) * pxPerMm_));
}

QString DrawingsWorkspace::drawingName() const {
    QString name = target_->currentText().trimmed();
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    return name.isEmpty() ? tr("drawing") : name;
}

bool DrawingsWorkspace::ensureDrawing() {
    if (stale_) regenerate();
    if (drawing_) return true;
    QMessageBox::warning(this, tr("Drawing"), status_->text());
    return false;
}

void DrawingsWorkspace::exportPdf() {
    if (!ensureDrawing()) return;
    const QString file = QFileDialog::getSaveFileName(this, tr("Export drawing as PDF"), drawingName() + QStringLiteral(".pdf"),
                                                      tr("PDF files (*.pdf)"));
    if (file.isEmpty()) return;
    auto r = exportDrawingPdf(*drawing_, file.toStdString(), drawingName().toStdString());
    if (!r) QMessageBox::critical(this, tr("Export failed"), QString::fromStdString(r.error().message));
}

void DrawingsWorkspace::exportSvg() {
    if (!ensureDrawing()) return;
    const QString file = QFileDialog::getSaveFileName(this, tr("Export drawing as SVG"), drawingName() + QStringLiteral(".svg"),
                                                      tr("SVG files (*.svg)"));
    if (file.isEmpty()) return;
    auto r = exportDrawingSvg(*drawing_, file.toStdString());
    if (!r) QMessageBox::critical(this, tr("Export failed"), QString::fromStdString(r.error().message));
    else if (r.value().size() > 1) QMessageBox::information(this, tr("Export"), tr("Written:\n%1").arg(filesText(r.value())));
}

void DrawingsWorkspace::exportDxf() {
    if (!ensureDrawing()) return;
    const QString file = QFileDialog::getSaveFileName(this, tr("Export drawing views as DXF"),
                                                      drawingName() + QStringLiteral(".dxf"), tr("DXF files (*.dxf)"));
    if (file.isEmpty()) return;
    auto r = exportDrawingDxf(*drawing_, file.toStdString());
    if (!r) QMessageBox::critical(this, tr("Export failed"), QString::fromStdString(r.error().message));
    else
        QMessageBox::information(this, tr("Export"),
                                 tr("One DXF file per view, model space 1:1 in millimetres:\n%1").arg(filesText(r.value())));
}

void DrawingsWorkspace::print() {
    if (!ensureDrawing()) return;
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("Print drawing"));
    if (dialog.exec() != QDialog::Accepted) return;
    printing::PrintOptions options;
    options.mode = printing::PrintMode::ExactScale;
    options.plan.tileLabel = tr("{row}/{col}").toStdString();
    options.plan.scaledLabel = tr("Scaled to {percent} % — not to scale").toStdString();
    std::vector<printing::PrintPlan> plans;
    auto r = printing::printSheets(printer, drawing_->sheets, options, &plans);
    if (!r && r.error().code == core::ErrorCode::ConstraintViolation) {
        const auto answer = QMessageBox::question(
            this, tr("Print drawing"),
            tr("The sheet does not fit the printable area at exact scale:\n%1\n\n"
               "Print it scaled to fit the page instead? The printout will then NOT be to scale "
               "(the scale factor is printed on the page).")
                .arg(QString::fromStdString(r.error().message)));
        if (answer != QMessageBox::Yes) return;
        options.mode = printing::PrintMode::FitToPage;
        plans.clear();
        r = printing::printSheets(printer, drawing_->sheets, options, &plans);
    }
    if (!r) {
        QMessageBox::critical(this, tr("Printing failed"), QString::fromStdString(r.error().message));
        return;
    }
    QString report;
    for (const auto& plan : plans) {
        report += tr("Scale factor on paper: %1 %").arg(plan.scaleFactor * 100.0, 0, 'f', 1) + QLatin1Char('\n');
        if (plan.calibrationMarkPresent)
            report += tr("Measure the calibration mark: it must be %1 mm long.").arg(plan.calibrationPrintedLength, 0, 'f', 1) +
                      QLatin1Char('\n');
    }
    report += tr("The application cannot detect scaling applied by the printer driver.");
    QMessageBox::information(this, tr("Printed"), report);
}

} // namespace fos::app::gui
