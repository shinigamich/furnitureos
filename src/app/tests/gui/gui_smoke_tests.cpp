// SPDX-License-Identifier: AGPL-3.0-only
// GUI smoke test (label `gui`): needs a display (CTest runs it under xvfb-run); exits with 77
// (skipped) without one. Optional: FOS_APP_SHOT_DIR=<dir> writes 1440x900 screenshots of the
// four workspaces in English and Ukrainian.
#include "cutting_workspace.hpp"
#include "drawing_annotations.hpp"
#include "drawings_workspace.hpp"
#include "fos/app/drawing_edits.hpp"
#include "fos/app/interior_rows.hpp"
#include "inspector_panel.hpp"
#include "interior_editor.hpp"
#include "main_window.hpp"
#include "production_workspace.hpp"
#include "project_browser.hpp"

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollBar>
#include <QPushButton>
#include <QSpinBox>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>

#include <gtest/gtest.h>

namespace fos::app::gui {
namespace {

double width(Session& s) { return s.project().items.at(0).cabinet()->parameters.value("width").valueOr(-1); }

void settle(int ms = 200) {
    QApplication::processEvents();
    QTest::qWait(ms);
}

/// True when the image contains something other than white paper.
bool hasInk(const QImage& image) {
    for (int y = 0; y < image.height(); y += 3)
        for (int x = 0; x < image.width(); x += 3)
            if (qGray(image.pixel(x, y)) < 128) return true;
    return false;
}

TEST(GuiSmoke, TemplateWidthDrawingsSaveReopen) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    MainWindow w;
    w.show();
    ASSERT_TRUE(w.newFromTemplate(TemplateKind::BaseCabinet, std::nullopt));
    settle();
    // 2 sides, bottom, 2 rails, plinth, back, shelf, 2 doors.
    EXPECT_EQ(w.browser()->partCount(), 10);
    EXPECT_EQ(w.session().selection().itemId, "cab1");

    // Width 900 through the inspector editor (as typed + Enter).
    ASSERT_TRUE(w.inspector()->submitParameter(QStringLiteral("width"), QStringLiteral("900")));
    settle();
    EXPECT_DOUBLE_EQ(width(w.session()), 900);
    EXPECT_DOUBLE_EQ(w.session().findPart("cab1/shelf.1")->length, 862); // 900 - 2*18 - 2*1
    EXPECT_TRUE(w.windowTitle().contains(QLatin1Char('*')));

    // Invalid input: inline engine message, nothing changes.
    EXPECT_FALSE(w.inspector()->submitParameter(QStringLiteral("width"), QStringLiteral("width +")));
    EXPECT_FALSE(w.inspector()->parameterError(QStringLiteral("width")).isEmpty());
    EXPECT_DOUBLE_EQ(width(w.session()), 900);

    // Drawings workspace renders a non-empty preview of the assembly drawing.
    w.workspaces()->setCurrentWidget(w.drawingsWorkspace());
    settle();
    ASSERT_NE(w.drawingsWorkspace()->drawing(), nullptr);
    ASSERT_FALSE(w.drawingsWorkspace()->preview().isNull());
    EXPECT_TRUE(hasInk(w.drawingsWorkspace()->preview()));
    w.workspaces()->setCurrentWidget(w.productionWorkspace());
    settle();
    EXPECT_GT(w.productionWorkspace()->cutListRows(), 0);
    w.workspaces()->setCurrentWidget(w.cuttingWorkspace());
    settle();
    EXPECT_EQ(w.cuttingWorkspace()->materialCount(), 2);

    const QString file = dir.filePath(QStringLiteral("smoke.fosp"));
    ASSERT_TRUE(w.saveToFile(file));
    EXPECT_FALSE(w.windowTitle().contains(QLatin1Char('*')));

    MainWindow reopened;
    ASSERT_TRUE(reopened.openFile(file, false));
    settle();
    EXPECT_DOUBLE_EQ(width(reopened.session()), 900);
    EXPECT_EQ(reopened.browser()->partCount(), 10);
    EXPECT_FALSE(reopened.session().isDirty());
}


/// Child widget of the current inspector content (it is rebuilt after every change).
template <class W> W* inspectorChild(MainWindow& w, const char* name) {
    return w.inspector()->findChild<W*>(QString::fromLatin1(name));
}

void click(QPushButton* button) {
    ASSERT_NE(button, nullptr);
    ASSERT_TRUE(button->isEnabled()) << button->objectName().toStdString();
    button->click();
    settle(100);
}

/// Selects the section in the interior tab (after the inspector has been rebuilt).
void selectSection(MainWindow& w, const char* path) {
    ASSERT_NE(w.inspector()->interiorEditor(), nullptr);
    ASSERT_TRUE(w.inspector()->interiorEditor()->selectSection(QString::fromLatin1(path)));
}

void selectCombo(QComboBox* combo, int value) {
    ASSERT_NE(combo, nullptr);
    const int index = combo->findData(value);
    ASSERT_GE(index, 0);
    combo->setCurrentIndex(index);
}

/// Scenario: base cabinet -> split interior vertically -> left section 2 shelves -> door pair
/// -> resolved parts checked by hand -> undo restores the template.
///
/// Hand arithmetic (template: 600 x 820 x 560, 18 mm board, plinth 100, rails on top,
/// overlay back, shelf clearance 1, shelf setback 20, front reveal 2, front gap 3):
///   inner width 600 - 2*18 = 564; partition 18 -> two shares of (564 - 18) / 2 = 273
///   shelves in section 1: 273 - 2*1 = 271 long, 560 - 20 = 540 deep
///   partition: inner height 820 - 100 - 18 - 18 = 684 long
///   door pair: front 564 + 2*(18 - 2) = 596 wide, 684 + 2*16 = 716 high; each door (596 - 3) / 2 = 296.5
TEST(GuiSmoke, InteriorSplitShelvesDoorPairUndo) {
    MainWindow w;
    w.show();
    ASSERT_TRUE(w.newFromTemplate(TemplateKind::BaseCabinet, std::nullopt));
    settle();
    Session& s = w.session();
    ASSERT_NE(w.inspector()->cabinetTabs(), nullptr);
    w.inspector()->cabinetTabs()->setCurrentIndex(2); // Interior
    settle(100);

    selectSection(w, "");
    click(inspectorChild<QPushButton>(w, "splitVertical"));
    ASSERT_EQ(s.project().items[0].cabinet()->interior.children.size(), 2u);
    EXPECT_EQ(w.inspector()->cabinetTabs()->currentIndex(), 2); // the tab survives the rebuild

    selectSection(w, "1");
    auto* shelves = inspectorChild<QSpinBox>(w, "shelfCount");
    ASSERT_NE(shelves, nullptr);
    ASSERT_TRUE(shelves->isEnabled());
    shelves->setValue(2);
    settle(100);
    EXPECT_EQ(s.project().items[0].cabinet()->interior.children[0].shelfCount, 2);

    // Door pair on the whole interior, handle centred (the template had it at the top).
    selectSection(w, "");
    selectCombo(inspectorChild<QComboBox>(w, "frontKind"), static_cast<int>(FrontInput::Choice::DoorPair));
    selectCombo(inspectorChild<QComboBox>(w, "handlePosition"), static_cast<int>(furniture::HandlePosition::Center));
    click(inspectorChild<QPushButton>(w, "applyFront"));
    ASSERT_TRUE(s.project().items[0].cabinet()->interior.front.has_value());
    EXPECT_EQ(s.project().items[0].cabinet()->interior.front->handle, furniture::HandlePosition::Center);

    // Resolved part list: 2 sides, bottom, 2 rails, plinth, back, partition, 2 shelves, 2 doors.
    EXPECT_EQ(w.browser()->partCount(), 12);
    const auto* partition = s.findPart("cab1/partition.1");
    ASSERT_NE(partition, nullptr);
    EXPECT_DOUBLE_EQ(partition->length, 684);
    EXPECT_DOUBLE_EQ(partition->thickness, 18);
    for (const char* id : {"cab1/shelf.1.1", "cab1/shelf.1.2"}) {
        const auto* shelf = s.findPart(id);
        ASSERT_NE(shelf, nullptr) << id;
        EXPECT_DOUBLE_EQ(shelf->length, 271);
        EXPECT_DOUBLE_EQ(shelf->width, 540);
    }
    EXPECT_EQ(s.findPart("cab1/shelf.2.1"), nullptr);
    for (const char* id : {"cab1/door.left", "cab1/door.right"}) {
        const auto* door = s.findPart(id);
        ASSERT_NE(door, nullptr) << id;
        EXPECT_DOUBLE_EQ(door->length, 716);
        EXPECT_DOUBLE_EQ(door->width, 296.5);
    }

    // A refused size shows the engine's message inline and changes nothing.
    selectSection(w, "1");
    const auto before = s.project().items[0].cabinet()->interior;
    auto* size = inspectorChild<QLineEdit>(w, "sectionSize");
    ASSERT_NE(size, nullptr);
    size->setText(QStringLiteral("600"));
    click(inspectorChild<QPushButton>(w, "setSectionSize"));
    EXPECT_EQ(s.project().items[0].cabinet()->interior, before);
    auto* error = inspectorChild<QLabel>(w, "interiorError");
    ASSERT_NE(error, nullptr);
    EXPECT_FALSE(error->isHidden());
    EXPECT_FALSE(error->text().isEmpty());

    // Undo the three steps: the template interior and its 10 parts are back.
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(s.undo().ok());
    settle();
    EXPECT_EQ(w.browser()->partCount(), 10);
    EXPECT_DOUBLE_EQ(s.findPart("cab1/shelf.1")->length, 562);
    EXPECT_EQ(s.findPart("cab1/partition.1"), nullptr);
    EXPECT_EQ(s.project().items[0].cabinet()->interior.front->handle, furniture::HandlePosition::Top);
}

TEST(GuiSmoke, PartOverrideHardwareDrawingEditsCutting) {
    MainWindow w;
    w.show();
    ASSERT_TRUE(w.newFromTemplate(TemplateKind::BaseCabinet, std::nullopt));
    settle();
    Session& s = w.session();

    // Hardware tab lists the library's hinge.
    w.inspector()->cabinetTabs()->setCurrentIndex(1);
    auto* hinge = inspectorChild<QComboBox>(w, "hardware:0");
    ASSERT_NE(hinge, nullptr);
    EXPECT_EQ(hinge->count(), 1);

    // Part override: shelf material MDF from the part inspector.
    s.selectPart("cab1/shelf.1");
    settle(100);
    auto* material = inspectorChild<QComboBox>(w, "overrideMaterial");
    ASSERT_NE(material, nullptr);
    const int mdf = material->findData(QString::fromLatin1(project::defaults::kMdf18));
    ASSERT_GE(material->findData(QString::fromLatin1(project::defaults::kMdf18)), 0);
    material->setCurrentIndex(mdf);
    emit material->activated(mdf);
    settle(100);
    EXPECT_EQ(s.findPart("cab1/shelf.1")->material.str(), project::defaults::kMdf18);

    // Drawings: click on the overall width dimension of the assembly drawing, override its text.
    s.selectItem("cab1");
    w.workspaces()->setCurrentWidget(w.drawingsWorkspace());
    settle();
    const auto* drawing = w.drawingsWorkspace()->drawing();
    ASSERT_NE(drawing, nullptr);
    const std::string widthDim = "cab1/view.front/dim.overall.width";
    std::optional<geometry::Vec2> onLine;
    double longest = 0;
    for (const auto& p : drawing->sheets[0].primitives)
        if (const auto* l = std::get_if<drawings::Line>(&p); l && l->elementId == widthDim) {
            const double len = geometry::length(l->b - l->a);
            if (len > longest) {
                longest = len;
                onLine = (l->a + l->b) * 0.5;
            }
        }
    ASSERT_TRUE(onLine.has_value());
    w.drawingsWorkspace()->clickPaper(onLine->x, onLine->y);
    EXPECT_EQ(w.drawingsWorkspace()->annotations()->selectedDimension(), widthDim);
    auto* text = w.drawingsWorkspace()->findChild<QLineEdit*>(QStringLiteral("dimensionText"));
    ASSERT_NE(text, nullptr);
    text->setText(QStringLiteral("about 600"));
    click(w.drawingsWorkspace()->findChild<QPushButton*>(QStringLiteral("applyText")));
    const auto& edit = s.project().drawingEdits.at("cab1/drawing.assembly").elements.at(widthDim);
    EXPECT_EQ(edit.textOverride, std::optional<std::string>("about 600"));
    EXPECT_EQ(edit.measuredAtEdit, std::optional<double>(600));
    EXPECT_EQ(w.drawingsWorkspace()->annotations()->selectedDimension(), widthDim); // still selected
    // Width 700 -> the stale override is listed among the findings.
    ASSERT_TRUE(s.execute(project::commands::setParameter("cab1", "width", 700)).ok());
    settle();
    auto* findings = w.drawingsWorkspace()->findChild<QListWidget*>(QStringLiteral("editFindings"));
    ASSERT_NE(findings, nullptr);
    ASSERT_GE(findings->count(), 1);
    EXPECT_TRUE(findings->item(0)->text().contains(QStringLiteral("600")));
    EXPECT_TRUE(findings->item(0)->text().contains(QStringLiteral("700")));

    // Cutting: objective and a remnant for particleboard.
    w.workspaces()->setCurrentWidget(w.cuttingWorkspace());
    settle();
    auto* cutting = w.cuttingWorkspace();
    const int pb = cutting->settingsScope()->findData(QString::fromLatin1(project::defaults::kParticleboard18));
    ASSERT_GE(pb, 0);
    cutting->settingsScope()->setCurrentIndex(pb);
    emit cutting->settingsScope()->activated(pb);
    selectCombo(cutting->objective(), static_cast<int>(cutting::Objective::PreferRemnants));
    click(cutting->findChild<QPushButton*>(QStringLiteral("addRemnant")));
    EXPECT_EQ(cutting->remnants()->rowCount(), 1);
    cutting->apply();
    settle();
    const auto& own = s.project().cutting.forMaterial(project::defaults::kParticleboard18);
    EXPECT_EQ(own.objective, cutting::Objective::PreferRemnants);
    ASSERT_EQ(own.remnants.size(), 1u);
    EXPECT_EQ(own.remnants[0].id, "R1");
    EXPECT_DOUBLE_EQ(own.remnants[0].length, 1000);
}

void screenshots(const QString& language) {
    const QString shotDir = qEnvironmentVariable("FOS_APP_SHOT_DIR");
    QTranslator translator;
    if (language != QLatin1String("en")) {
        ASSERT_TRUE(translator.load(QStringLiteral("furnitureos_") + language, QStringLiteral(FOS_APP_QM_DIR)));
        QApplication::installTranslator(&translator);
    }
    {
        MainWindow w;
        w.resize(1440, 900);
        w.show();
        ASSERT_TRUE(w.newFromTemplate(TemplateKind::BaseCabinet, std::nullopt));
        ASSERT_TRUE(w.inspector()->submitParameter(QStringLiteral("width"), QStringLiteral("900")));
        settle(300);
        if (language == QLatin1String("uk")) {
            EXPECT_EQ(w.workspaces()->tabText(1), QString::fromUtf8("Креслення"));
            EXPECT_EQ(w.workspaces()->tabText(3), QString::fromUtf8("Розкрій"));
        }
        const char* names[] = {"3d", "drawings", "production", "cutting"};
        for (int i = 0; i < 4; ++i) {
            w.workspaces()->setCurrentIndex(i);
            settle(500);
            const QImage shot = w.grab().toImage();
            EXPECT_EQ(shot.width(), 1440);
            if (!shotDir.isEmpty()) {
                QDir().mkpath(shotDir);
                EXPECT_TRUE(shot.save(QDir(shotDir).filePath(QStringLiteral("%1-%2.png").arg(language, QLatin1String(names[i])))));
            }
        }
        auto save = [&](const char* name) {
            settle(400);
            const QImage shot = w.grab().toImage();
            EXPECT_EQ(shot.width(), 1440);
            if (!shotDir.isEmpty()) {
                EXPECT_TRUE(shot.save(QDir(shotDir).filePath(QStringLiteral("%1-%2.png").arg(language, QLatin1String(name)))));
            }
        };
        auto scrollInspectorToEnd = [&] {
            settle(300);
            w.inspector()->verticalScrollBar()->setValue(w.inspector()->verticalScrollBar()->maximum());
        };
        Session& s = w.session();
        // A part selected: the inspector shows its read-only facts and the override editor.
        s.selectPart("cab1/shelf.1");
        w.workspaces()->setCurrentIndex(0);
        save("part");
        scrollInspectorToEnd();
        save("part-override");

        // Interior editor: split vertically, left section 2 shelves, section 1 selected.
        s.selectItem("cab1");
        settle(100);
        ASSERT_TRUE(s.execute(splitCommand("cab1", "", furniture::SplitDirection::Vertical, 2)).ok());
        ASSERT_TRUE(s.execute(project::commands::setShelfCount("cab1", "1", 2)).ok());
        settle(100);
        w.inspector()->cabinetTabs()->setCurrentIndex(2);
        selectSection(w, "1");
        {
            settle(300);
            auto* t = w.inspector()->cabinetTabs();
            auto* c = w.inspector()->widget();
            std::fprintf(stderr, "DEBUG content hint %d min %d h %d | tabs hint %d min %d h %d cur %d\n", c->sizeHint().height(), c->minimumSizeHint().height(), c->height(), t->sizeHint().height(), t->minimumSizeHint().height(), t->height(), t->currentIndex());
            for (int i = 0; i < t->count(); ++i) std::fprintf(stderr, "DEBUG page %d hint %d min %d pol %d h %d\n", i, t->widget(i)->sizeHint().height(), t->widget(i)->minimumSizeHint().height(), (int)t->widget(i)->sizePolicy().verticalPolicy(), t->widget(i)->height());
        }
        save("interior");
        scrollInspectorToEnd();
        save("interior-front");
        w.inspector()->cabinetTabs()->setCurrentIndex(1);
        settle(100);
        scrollInspectorToEnd();
        save("hardware");

        // Drawings: a user dimension, a text override made stale by a width change, one selected.
        w.workspaces()->setCurrentWidget(w.drawingsWorkspace());
        settle(300);
        DrawingRequest req;
        req.id = "cab1";
        auto* drawing = w.drawingsWorkspace()->drawing();
        ASSERT_NE(drawing, nullptr);
        const auto front = findDimension(*drawing, "cab1/view.front/dim.overall.width");
        ASSERT_TRUE(front.has_value());
        const auto ctx = editContext(*s.resolved(), req);
        const auto& view = drawing->views[front->viewIndex];
        ASSERT_TRUE(s.execute(addUserDimensionCommand(s.project(), drawingIdOf(req), view, ctx,
                                                      drawings::DimensionOrientation::Horizontal,
                                                      {"cab1/shelf.1.1", 0, {}}, {"cab1/shelf.1.1", 1, {}}, 12)
                                  .value())
                        .ok());
        settle(300);
        drawing = w.drawingsWorkspace()->drawing();
        ASSERT_TRUE(s.execute(dimensionTextCommand(s.project(), drawingIdOf(req),
                                                   *findDimension(*drawing, "cab1/view.front/dim.overall.width"),
                                                   QStringLiteral("900 (check)")))
                        .ok());
        ASSERT_TRUE(s.execute(project::commands::setParameter("cab1", "width", 960)).ok());
        settle(300);
        w.drawingsWorkspace()->annotations()->selectDimension("user.dim.1");
        save("drawings-annotations");

        w.workspaces()->setCurrentWidget(w.cuttingWorkspace());
        settle(300);
        auto* cutting = w.cuttingWorkspace();
        const int pb = cutting->settingsScope()->findData(QString::fromLatin1(project::defaults::kParticleboard18));
        cutting->settingsScope()->setCurrentIndex(pb);
        emit cutting->settingsScope()->activated(pb);
        selectCombo(cutting->objective(), static_cast<int>(cutting::Objective::PreferRemnants));
        cutting->findChild<QPushButton*>(QStringLiteral("addRemnant"))->click();
        cutting->apply();
        save("cutting-remnants");
    }
    if (language != QLatin1String("en")) QApplication::removeTranslator(&translator);
}

TEST(GuiSmoke, ScreenshotsEnglish) { screenshots(QStringLiteral("en")); }
TEST(GuiSmoke, ScreenshotsUkrainian) { screenshots(QStringLiteral("uk")); }

} // namespace
} // namespace fos::app::gui

int main(int argc, char** argv) {
    const bool offscreen = qEnvironmentVariable("QT_QPA_PLATFORM") == QLatin1String("offscreen");
    if (!offscreen && qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        std::fprintf(stderr, "No display: GUI smoke test skipped.\n");
        return 77;
    }
    QApplication app(argc, argv);
    // Isolated settings so the test never touches the user's preferences or recent files.
    QTemporaryDir settingsDir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QStandardPaths::setTestModeEnabled(true);
    QApplication::setOrganizationName(QStringLiteral("FurnitureOS-test"));
    QApplication::setApplicationName(QStringLiteral("furnitureos-gui-test"));
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
