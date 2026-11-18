// SPDX-License-Identifier: AGPL-3.0-only
#include "shape_editor.hpp"

#include "form_widgets.hpp"
#include "fos/app/session.hpp"
#include "inspector_panel.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fos::app::gui {

namespace {
constexpr int kAutomaticBand = -1;

QString cell(const QTableWidget* table, int row, int column) {
    const QTableWidgetItem* item = table->item(row, column);
    return item ? item->text() : QString();
}

void setCell(QTableWidget* table, int row, int column, const QString& text) {
    table->setItem(row, column, new QTableWidgetItem(text));
}

QTableWidget* makeTable(QWidget* parent, const QStringList& header, const char* name) {
    auto* table = new QTableWidget(0, static_cast<int>(header.size()), parent);
    table->setObjectName(QString::fromLatin1(name));
    table->setHorizontalHeaderLabels(header);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->horizontalHeader()->setMinimumSectionSize(36);
    table->verticalHeader()->setDefaultSectionSize(26);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setMinimumHeight(110);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return table;
}

QPushButton* button(const QString& text, const char* name, QWidget* parent) {
    auto* b = new QPushButton(text, parent);
    b->setObjectName(QString::fromLatin1(name));
    return b;
}

QComboBox* edgeBox(QWidget* parent, bool automatic) {
    auto* box = narrowCombo(parent, 4);
    if (automatic) box->addItem(ShapeEditor::tr("Auto"), kAutomaticBand);
    for (domain::EdgeSide side : domain::kAllEdges)
        box->addItem(QString::fromLatin1(domain::toString(side)), static_cast<int>(side));
    return box;
}

int currentRow(const QTableWidget* table) {
    const int row = table->currentRow();
    return row >= 0 ? row : table->rowCount() - 1;
}
/// Shows page `index`; the hidden pages do not count for the size (no empty space below a
/// short page).
void showPage(QStackedWidget* pages, int index) {
    pages->setCurrentIndex(index);
    for (int i = 0; i < pages->count(); ++i)
        pages->widget(i)->setSizePolicy(QSizePolicy::Preferred,
                                        i == index ? QSizePolicy::Preferred : QSizePolicy::Ignored);
    pages->adjustSize();
}

} // namespace

ShapeEditor::ShapeEditor(Session& session, ContourTarget target, QWidget* parent)
    : QGroupBox(tr("Shape"), parent), session_(session), target_(std::move(target)) {
    setObjectName(QStringLiteral("shapeEditor"));
    auto* layout = new QVBoxLayout(this);
    auto* top = new QFormLayout;
    top->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    kind_ = choiceBox(contourKindChoices(), furniture::ContourKind::Rectangle, this);
    kind_->setObjectName(QStringLiteral("shapeKind"));
    top->addRow(tr("Outline:"), kind_);
    layout->addLayout(top);

    pages_ = new QStackedWidget(this);
    // Rectangle
    auto* plain = new QLabel(tr("Plain rectangle. Cut-outs below still apply."), pages_);
    plain->setWordWrap(true);
    pages_->addWidget(plain);
    // Rounded corners: (0,0), (L,0), (L,W), (0,W)
    auto* rounded = new QWidget(pages_);
    auto* rform = new QFormLayout(rounded);
    rform->setContentsMargins(0, 0, 0, 0);
    const QString corners[4] = {tr("Radius L1 · W1 (0, 0):"), tr("Radius L1 · W2 (L, 0):"),
                                tr("Radius L2 · W2 (L, W):"), tr("Radius L2 · W1 (0, W):")};
    for (int i = 0; i < 4; ++i) {
        radii_[i] = new QLineEdit(rounded);
        radii_[i]->setObjectName(QStringLiteral("shapeRadius:%1").arg(i));
        radii_[i]->setPlaceholderText(tr("sharp"));
        rform->addRow(corners[i], radii_[i]);
    }
    pages_->addWidget(rounded);
    // Arc edge
    auto* arc = new QWidget(pages_);
    auto* aform = new QFormLayout(arc);
    aform->setContentsMargins(0, 0, 0, 0);
    arcEdge_ = edgeBox(arc, false);
    arcEdge_->setObjectName(QStringLiteral("shapeArcEdge"));
    arcRise_ = new QLineEdit(arc);
    arcRise_->setObjectName(QStringLiteral("shapeArcRise"));
    arcDirection_ = choiceBox(arcDirectionChoices(), furniture::ArcDirection::Convex, arc);
    arcDirection_->setObjectName(QStringLiteral("shapeArcDirection"));
    aform->addRow(tr("Edge:"), arcEdge_);
    aform->addRow(tr("Rise (mm):"), arcRise_);
    aform->addRow(tr("Direction:"), arcDirection_);
    pages_->addWidget(arc);
    // Custom vertices
    auto* custom = new QWidget(pages_);
    auto* cv = new QVBoxLayout(custom);
    cv->setContentsMargins(0, 0, 0, 0);
    auto* vhint = new QLabel(tr("Vertices counter-clockwise; bulge 0 = straight segment to the next vertex, "
                                "1 = half circle, negative = clockwise arc."),
                             custom);
    vhint->setWordWrap(true);
    cv->addWidget(vhint);
    vertices_ = makeTable(custom, {tr("X"), tr("Y"), tr("Bulge"), tr("Band from")}, "shapeVertices");
    cv->addWidget(vertices_);
    auto* vbuttons = new QHBoxLayout;
    auto* addV = button(tr("Add"), "shapeAddVertex", custom);
    auto* removeV = button(tr("Remove"), "shapeRemoveVertex", custom);
    auto* up = button(tr("Up"), "shapeVertexUp", custom);
    auto* down = button(tr("Down"), "shapeVertexDown", custom);
    for (QPushButton* b : {addV, removeV, up, down}) vbuttons->addWidget(b);
    cv->addLayout(vbuttons);
    pages_->addWidget(custom);
    layout->addWidget(pages_);

    auto* cutoutTitle = new QLabel(tr("Cut-outs (through openings, never edge-banded):"), this);
    cutoutTitle->setWordWrap(true);
    layout->addWidget(cutoutTitle);
    cutouts_ = makeTable(this, {tr("Kind"), tr("X"), tr("Y"), tr("Length"), tr("Width"), tr("Radius")},
                         "shapeCutouts");
    cutouts_->setToolTip(tr("Rectangle: X, Y = lower-left corner, Length × Width, corner Radius (empty = sharp).\n"
                            "Circle: X, Y = centre, Radius."));
    cutouts_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    cutouts_->setColumnWidth(0, 84);
    cutouts_->setMaximumHeight(170);
    layout->addWidget(cutouts_);
    auto* cbuttons = new QHBoxLayout;
    auto* addRect = button(tr("Add rectangle"), "shapeAddRectCutout", this);
    auto* addCircle = button(tr("Add circle"), "shapeAddCircleCutout", this);
    auto* removeC = button(tr("Remove"), "shapeRemoveCutout", this);
    for (QPushButton* b : {addRect, addCircle, removeC}) cbuttons->addWidget(b);
    layout->addLayout(cbuttons);

    auto* note = new QLabel(tr("Values in mm, in the part frame (X along the length, Y along the width). "
                               "Each field accepts an expression of the parameters and contour.length, "
                               "contour.width, contour.thickness."),
                            this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(note);

    error_ = makeErrorLabel(this);
    error_->setObjectName(QStringLiteral("shapeError"));
    auto* actions = new QHBoxLayout;
    auto* applyButton = button(tr("Apply"), "shapeApply", this);
    auto* reset = button(tr("Reset"), "shapeReset", this);
    reset->setToolTip(tr("Discard the changes in this section and show the stored shape."));
    auto* remove = button(tr("Remove shape"), "shapeRemove", this);
    const auto stored = currentContour(session_.project(), target_);
    remove->setEnabled(stored.has_value());
    for (QPushButton* b : {applyButton, reset, remove}) actions->addWidget(b);
    layout->addLayout(actions);
    layout->addWidget(error_);

    connect(kind_, qOverload<int>(&QComboBox::currentIndexChanged), this, &ShapeEditor::kindChanged);
    connect(addV, &QPushButton::clicked, this, &ShapeEditor::addVertex);
    connect(removeV, &QPushButton::clicked, this, &ShapeEditor::removeVertex);
    connect(up, &QPushButton::clicked, this, [this] { moveVertex(-1); });
    connect(down, &QPushButton::clicked, this, [this] { moveVertex(1); });
    connect(addRect, &QPushButton::clicked, this, [this] { addCutout(furniture::CutoutKind::Rectangle); });
    connect(addCircle, &QPushButton::clicked, this, [this] { addCutout(furniture::CutoutKind::Circle); });
    connect(removeC, &QPushButton::clicked, this, &ShapeEditor::removeCutout);
    connect(applyButton, &QPushButton::clicked, this, &ShapeEditor::apply);
    connect(reset, &QPushButton::clicked, this, [this] {
        showError(error_, {});
        load(contourInput(currentContour(session_.project(), target_)));
    });
    connect(remove, &QPushButton::clicked, this, [this] { run(std::nullopt); });

    load(contourInput(stored));
}

void ShapeEditor::load(const ContourInput& input) {
    rows_ = input;
    const QSignalBlocker block(kind_);
    selectData(kind_, static_cast<int>(input.kind));
    showPage(pages_, kind_->currentIndex());
    for (int i = 0; i < 4; ++i) radii_[i]->setText(input.radii[static_cast<std::size_t>(i)]);
    selectData(arcEdge_, static_cast<int>(input.arcEdge));
    arcRise_->setText(input.arcRise);
    selectData(arcDirection_, static_cast<int>(input.arcDirection));
    fillVertices();
    fillCutouts();
}

void ShapeEditor::setInput(const ContourInput& input) { load(input); }

void ShapeEditor::fillVertices() {
    vertices_->setRowCount(static_cast<int>(rows_.vertices.size()));
    for (int r = 0; r < vertices_->rowCount(); ++r) {
        const VertexRow& v = rows_.vertices[static_cast<std::size_t>(r)];
        setCell(vertices_, r, 0, v.x);
        setCell(vertices_, r, 1, v.y);
        setCell(vertices_, r, 2, v.bulge);
        auto* band = edgeBox(vertices_, true);
        selectData(band, v.bandFrom ? static_cast<int>(*v.bandFrom) : kAutomaticBand);
        vertices_->setCellWidget(r, 3, band);
    }
}

void ShapeEditor::fillCutouts() {
    cutouts_->setRowCount(static_cast<int>(rows_.cutouts.size()));
    for (int r = 0; r < cutouts_->rowCount(); ++r) {
        const CutoutRow& c = rows_.cutouts[static_cast<std::size_t>(r)];
        auto* kind = choiceBox(cutoutKindChoices(), c.kind, cutouts_);
        kind->setMinimumContentsLength(4);
        cutouts_->setCellWidget(r, 0, kind);
        setCell(cutouts_, r, 1, c.x);
        setCell(cutouts_, r, 2, c.y);
        setCell(cutouts_, r, 3, c.length);
        setCell(cutouts_, r, 4, c.width);
        setCell(cutouts_, r, 5, c.radius);
    }
}

ContourInput ShapeEditor::input() const {
    ContourInput in;
    in.kind = valueOf<furniture::ContourKind>(kind_);
    for (int i = 0; i < 4; ++i) in.radii[static_cast<std::size_t>(i)] = radii_[i]->text();
    in.arcEdge = static_cast<domain::EdgeSide>(arcEdge_->currentData().toInt());
    in.arcRise = arcRise_->text();
    in.arcDirection = valueOf<furniture::ArcDirection>(arcDirection_);
    for (int r = 0; r < vertices_->rowCount(); ++r) {
        VertexRow v{cell(vertices_, r, 0), cell(vertices_, r, 1), cell(vertices_, r, 2), {}};
        if (const auto* band = qobject_cast<const QComboBox*>(vertices_->cellWidget(r, 3))) {
            const int value = band->currentData().toInt();
            if (value != kAutomaticBand) v.bandFrom = static_cast<domain::EdgeSide>(value);
        }
        in.vertices.push_back(v);
    }
    for (int r = 0; r < cutouts_->rowCount(); ++r) {
        CutoutRow c;
        if (const auto* kind = qobject_cast<const QComboBox*>(cutouts_->cellWidget(r, 0)))
            c.kind = valueOf<furniture::CutoutKind>(kind);
        c.x = cell(cutouts_, r, 1);
        c.y = cell(cutouts_, r, 2);
        c.length = cell(cutouts_, r, 3);
        c.width = cell(cutouts_, r, 4);
        c.radius = cell(cutouts_, r, 5);
        in.cutouts.push_back(c);
    }
    rows_ = in;
    return in;
}

void ShapeEditor::kindChanged() {
    showPage(pages_, kind_->currentIndex());
    // A new Custom outline starts from the full rectangle.
    if (valueOf<furniture::ContourKind>(kind_) == furniture::ContourKind::Custom && vertices_->rowCount() == 0) {
        input();
        rows_.vertices = rectangleVertexRows();
        fillVertices();
    }
}

void ShapeEditor::addVertex() {
    input();
    const int at = vertices_->currentRow() >= 0 ? vertices_->currentRow() + 1 : vertices_->rowCount();
    rows_.vertices.insert(rows_.vertices.begin() + at, VertexRow{QStringLiteral("0"), QStringLiteral("0"), {}, {}});
    fillVertices();
    vertices_->setCurrentCell(at, 0);
}

void ShapeEditor::removeVertex() {
    input();
    const int row = currentRow(vertices_);
    if (row < 0) return;
    rows_.vertices.erase(rows_.vertices.begin() + row);
    fillVertices();
}

void ShapeEditor::moveVertex(int delta) {
    input();
    const int row = vertices_->currentRow();
    if (row < 0) return;
    const auto to = moveRow(rows_.vertices, static_cast<std::size_t>(row), delta);
    fillVertices();
    if (to) vertices_->setCurrentCell(static_cast<int>(*to), 0);
}

void ShapeEditor::addCutout(furniture::CutoutKind kind) {
    input();
    CutoutRow row;
    row.kind = kind;
    row.x = QStringLiteral("100");
    row.y = QStringLiteral("100");
    if (kind == furniture::CutoutKind::Rectangle) {
        row.length = QStringLiteral("100");
        row.width = QStringLiteral("50");
    } else {
        row.radius = QStringLiteral("20");
    }
    rows_.cutouts.push_back(row);
    fillCutouts();
    cutouts_->setCurrentCell(cutouts_->rowCount() - 1, 1);
}

void ShapeEditor::removeCutout() {
    input();
    const int row = currentRow(cutouts_);
    if (row < 0) return;
    rows_.cutouts.erase(rows_.cutouts.begin() + row);
    fillCutouts();
}

bool ShapeEditor::run(std::optional<furniture::ContourSpec> spec) {
    auto r = session_.execute(contourCommand(target_, std::move(spec)));
    if (!r) {
        showError(error_, QString::fromStdString(r.error().message));
        return false;
    }
    showError(error_, {});
    return true;
}

bool ShapeEditor::apply() {
    auto spec = contourSpec(input());
    if (!spec) {
        showError(error_, QString::fromStdString(spec.error().message));
        return false;
    }
    return run(std::move(spec.value()));
}

QString ShapeEditor::errorText() const { return error_->isHidden() ? QString() : error_->text(); }

} // namespace fos::app::gui
