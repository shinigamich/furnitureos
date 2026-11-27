// SPDX-License-Identifier: AGPL-3.0-only
// GUI walk-through (label `gui`): every bundled sample and every template is opened, every
// workspace tab and dock is shown, the menu dialogs are opened and closed (Help > Check for
// updates is skipped: it would reach the network), common edits are undone / redone and the
// project is saved and reopened. In English and Ukrainian.
// Optional: FOS_APP_SHOT_DIR=<dir> writes screenshots of every workspace and dialog, and in
// Ukrainian mode <dir>/untranslated-uk.txt lists visible texts without a Cyrillic letter.
#include "fos/app/panel_tools.hpp"
#include "fos/app/samples.hpp"
#include "fos/app/templates.hpp"
#include "main_window.hpp"
#include "project_browser.hpp"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QTimer>
#include <QTranslator>

#include <gtest/gtest.h>

#include <functional>
#include <set>

namespace fos::app::gui {
namespace {

void settle(int ms = 150) {
    QApplication::processEvents();
    QTest::qWait(ms);
}

QString shotDir() { return qEnvironmentVariable("FOS_APP_SHOT_DIR"); }

void saveShot(QWidget* w, const QString& name) {
    const QImage shot = w->grab().toImage();
    EXPECT_FALSE(shot.isNull()) << name.toStdString();
    if (shotDir().isEmpty()) return;
    QDir().mkpath(shotDir());
    QString file = name;
    file.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    shot.save(QDir(shotDir()).filePath(file + QStringLiteral(".png")));
}

/// Visible texts without any Cyrillic letter but with a Latin word of 3+ letters (Ukrainian mode).
std::set<QString>& untranslated() {
    static std::set<QString> texts;
    return texts;
}

void collectUntranslated(QWidget* root, const QString& where) {
    static const QRegularExpression latinWord(QStringLiteral("[A-Za-z]{3,}"));
    static const QRegularExpression cyrillic(QStringLiteral("[\\x{0400}-\\x{04FF}]"));
    auto check = [&](const QString& raw) {
        QString text = raw;
        text.remove(QLatin1Char('&'));
        if (text.trimmed().isEmpty() || cyrillic.match(text).hasMatch() || !latinWord.match(text).hasMatch()) return;
        untranslated().insert(where + QStringLiteral(": ") + text.simplified().left(160));
    };
    QList<QWidget*> widgets = root->findChildren<QWidget*>();
    widgets.prepend(root);
    for (QWidget* w : widgets) {
        if (!w->isVisible()) continue;
        if (auto* l = qobject_cast<QLabel*>(w)) check(l->text());
        else if (auto* b = qobject_cast<QAbstractButton*>(w)) check(b->text());
        else if (auto* g = qobject_cast<QGroupBox*>(w)) check(g->title());
        if (auto* t = qobject_cast<QTabWidget*>(w))
            for (int i = 0; i < t->count(); ++i) check(t->tabText(i));
        if (auto* v = qobject_cast<QAbstractItemView*>(w); v && v->model())
            for (int c = 0; c < v->model()->columnCount(); ++c)
                check(v->model()->headerData(c, Qt::Horizontal).toString());
        if (w->isWindow()) check(w->windowTitle());
    }
}

/// Answers the modal dialogs `trigger` opens: each one is screenshot and then given to
/// `answer` (default: reject / Escape). `answer` returns false to fall back to the default.
using Answer = std::function<bool(QWidget*)>;
int runWithModals(const std::function<void()>& trigger, const QString& name, bool uk, const Answer& answer = {}) {
    int seen = 0;
    QMap<QWidget*, int> handled; // modal -> ticks since it was answered
    QTimer timer;
    timer.setInterval(50);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        QWidget* modal = QApplication::activeModalWidget();
        if (!modal) return;
        if (handled.contains(modal)) {
            // Still open 3 s after it was answered: the answer did not close it.
            if (++handled[modal] == 60) {
                ADD_FAILURE() << name.toStdString() << ": dialog '" << modal->windowTitle().toStdString()
                              << "' did not close when answered";
                if (auto* d = qobject_cast<QDialog*>(modal)) d->done(QDialog::Rejected);
                else modal->close();
            }
            return;
        }
        handled.insert(modal, 0);
        const int index = seen++;
        QTimer::singleShot(250, modal, [&, modal, index] {
            const QString shot = name + QStringLiteral("-%1").arg(index);
            saveShot(modal, shot);
            if (uk) collectUntranslated(modal, shot);
            if (answer && answer(modal)) return;
            if (auto* box = qobject_cast<QMessageBox*>(modal)) {
                if (box->escapeButton()) box->escapeButton()->click();
                else if (box->defaultButton()) box->defaultButton()->click();
                else box->reject();
            } else if (auto* dialog = qobject_cast<QDialog*>(modal)) {
                dialog->reject();
            } else {
                modal->close();
            }
        });
    });
    timer.start();
    trigger();
    settle(400);
    timer.stop();
    return seen;
}

/// Action by object name or by its (MainWindow-translated) menu text, '&' ignored.
QAction* findAction(MainWindow& w, const QString& objectNameOrText) {
    QString translated = QCoreApplication::translate("fos::app::gui::MainWindow", objectNameOrText.toUtf8().constData());
    translated.remove(QLatin1Char('&'));
    for (QAction* a : w.findChildren<QAction*>()) {
        QString text = a->text();
        text.remove(QLatin1Char('&'));
        if (a->objectName() == objectNameOrText || text == translated) return a;
    }
    return nullptr;
}

/// First item with a cabinet definition (worktops and plinths have none).
const project::Item* firstCabinet(Session& s) {
    for (const auto& item : s.project().items)
        if (item.cabinet()) return &item;
    return nullptr;
}

/// Undo / redo after common edits: width +100, duplicate, add panel, delete.
void undoRedoEdits(MainWindow& w) {
    Session& s = w.session();
    const project::Item* item = firstCabinet(s);
    ASSERT_NE(item, nullptr);
    const std::string id = item->id();
    s.selectItem(id);
    settle();
    const double width = item->cabinet()->parameters.value("width").valueOr(-1);
    const std::size_t items = s.project().items.size();
    const int parts = w.browser()->partCount();

    ASSERT_TRUE(s.execute(project::commands::setParameter(id, "width", width + 100)).ok());
    settle();
    EXPECT_DOUBLE_EQ(s.project().findItem(id)->cabinet()->parameters.value("width").valueOr(-1), width + 100);
    ASSERT_TRUE(s.undo().ok());
    settle();
    EXPECT_DOUBLE_EQ(s.project().findItem(id)->cabinet()->parameters.value("width").valueOr(-1), width);
    EXPECT_EQ(w.browser()->partCount(), parts);
    ASSERT_TRUE(s.redo().ok());
    settle();
    EXPECT_DOUBLE_EQ(s.project().findItem(id)->cabinet()->parameters.value("width").valueOr(-1), width + 100);
    ASSERT_TRUE(s.undo().ok());

    s.selectItem(id);
    QAction* duplicate = nullptr;
    for (QAction* a : w.findChildren<QAction*>())
        if (a->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_D)) duplicate = a;
    ASSERT_NE(duplicate, nullptr);
    duplicate->trigger();
    settle();
    EXPECT_EQ(s.project().items.size(), items + 1);
    ASSERT_TRUE(s.undo().ok());
    settle();
    EXPECT_EQ(s.project().items.size(), items);

    AddPanelRequest panel;
    panel.materialId = project::defaults::kParticleboard18;
    panel.placement = PanelPlacement::Origin;
    panel.length = 600;
    panel.width = 300;
    if (applyAddPanel(s, panel).ok()) {
        settle();
        EXPECT_EQ(s.project().items.size(), items + 1);
        ASSERT_TRUE(s.undo().ok());
        ASSERT_TRUE(s.redo().ok());
        ASSERT_TRUE(s.undo().ok());
    }
    EXPECT_EQ(s.project().items.size(), items);

    s.selectItem(id);
    settle();
    ASSERT_TRUE(s.execute(project::commands::removeItem(id)).ok());
    settle();
    EXPECT_EQ(s.project().items.size(), items - 1);
    ASSERT_TRUE(s.undo().ok());
    settle();
    EXPECT_EQ(s.project().items.size(), items);
    EXPECT_EQ(w.browser()->partCount(), parts);
}

/// Every workspace tab (screenshot each), then every dock shown and raised.
void visitWorkspaces(MainWindow& w, const QString& prefix, bool uk) {
    for (int i = 0; i < w.workspaces()->count(); ++i) {
        w.workspaces()->setCurrentIndex(i);
        settle(350);
        ASSERT_TRUE(w.workspaces()->currentWidget()->isVisible());
        const QString name = prefix + QStringLiteral("-tab%1").arg(i);
        saveShot(&w, name);
        if (uk) collectUntranslated(&w, name);
    }
    w.workspaces()->setCurrentIndex(0);
    for (QDockWidget* dock : w.findChildren<QDockWidget*>()) {
        dock->show();
        dock->raise();
        settle(80);
        EXPECT_TRUE(dock->isVisible()) << dock->objectName().toStdString();
    }
    settle(200);
    saveShot(&w, prefix + QStringLiteral("-docks"));
}

void saveAndReopen(MainWindow& w, const QString& file) {
    Session& s = w.session();
    ASSERT_TRUE(w.saveToFile(file));
    EXPECT_FALSE(s.isDirty());
    MainWindow reopened;
    reopened.resize(1440, 900);
    reopened.show();
    ASSERT_TRUE(reopened.openFile(file, false));
    settle();
    EXPECT_EQ(reopened.session().project().items.size(), s.project().items.size());
    EXPECT_EQ(reopened.browser()->partCount(), w.browser()->partCount());
    EXPECT_FALSE(reopened.session().isDirty());
    EXPECT_EQ(reopened.session().validationReport().counts.errors, s.validationReport().counts.errors);
    EXPECT_EQ(reopened.session().validationReport().counts.warnings, s.validationReport().counts.warnings);
}

/// Every menu dialog of the window (on the open project), closed without changes.
void openDialogs(MainWindow& w, const QString& prefix, bool uk, const QString& otherProject) {
    Session& s = w.session();
    const project::Item* item = firstCabinet(s);
    if (item) s.selectItem(item->id());
    settle();
    const std::size_t undoBefore = s.document().undoCount();

    auto run = [&](const QString& action, const QString& name, const Answer& answer = {}) -> int {
        QAction* a = findAction(w, action);
        if (!a) {
            ADD_FAILURE() << "no action " << action.toStdString();
            return 0;
        }
        if (!a->isEnabled()) {
            ADD_FAILURE() << action.toStdString() << " is disabled with an item selected";
            return 0;
        }
        return runWithModals([a] { a->trigger(); }, prefix + QStringLiteral("-dlg-") + name, uk, answer);
    };
    // A file dialog is answered with `otherProject`; later dialogs are rejected.
    const Answer pickFile = [&](QWidget* modal) {
        auto* files = qobject_cast<QFileDialog*>(modal);
        if (!files) return false;
        // Typed into the file-name field as a user would (the Qt dialog lists folders asynchronously).
        auto* edit = files->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
        if (!edit) return false;
        edit->setText(otherProject);
        static_cast<QDialog*>(files)->accept(); // virtual: QFileDialog::accept() checks the typed path
        return true;
    };

    EXPECT_GE(run(QStringLiteral("&Preferences…"), QStringLiteral("preferences")), 1);
    EXPECT_GE(run(QStringLiteral("actionPricingSettings"), QStringLiteral("pricing")), 1);
    EXPECT_GE(run(QStringLiteral("actionAddPanel"), QStringLiteral("addpanel")), 1);
    EXPECT_GE(run(QStringLiteral("actionTransform"), QStringLiteral("transform")), 1);
    EXPECT_GE(run(QStringLiteral("actionArray"), QStringLiteral("array")), 1);
    EXPECT_GE(run(QStringLiteral("importItemsFromProject"), QStringLiteral("import"), pickFile), 2);
    EXPECT_GE(run(QStringLiteral("actionCompareWith"), QStringLiteral("compare"), pickFile), 2);
    EXPECT_GE(run(QStringLiteral("actionExportJobPack"), QStringLiteral("jobpack")), 1);
    EXPECT_GE(run(QStringLiteral("actionAddWorktop"), QStringLiteral("worktop")), 1);
    EXPECT_GE(run(QStringLiteral("actionAddPlinth"), QStringLiteral("plinth")), 1);
    EXPECT_GE(run(QStringLiteral("&Keyboard shortcuts"), QStringLiteral("shortcuts")), 1);
    EXPECT_GE(run(QStringLiteral("&About FurnitureOS"), QStringLiteral("about")), 1);
    EXPECT_GE(run(QStringLiteral("&Welcome…"), QStringLiteral("welcome")), 1);
    // Template size dialogs (New from template / Add cabinet share TemplateSizeDialog).
    for (TemplateKind kind : allTemplates()) {
        QAction* add = nullptr;
        for (QAction* a : w.menuBar()->findChildren<QAction*>())
            if (a->text() == templateTitle(kind) + QStringLiteral("…")) add = a;
        ASSERT_NE(add, nullptr);
        EXPECT_GE(runWithModals([add] { add->trigger(); },
                                prefix + QStringLiteral("-dlg-size-") + QLatin1String(templateToken(kind)), uk),
                  1);
    }
    // Non-modal windows.
    for (const auto& [action, name] : {std::pair{"actionLibraries", "libraries"}, {"actionProductionBatch", "batch"}}) {
        QAction* a = findAction(w, QLatin1String(action));
        ASSERT_NE(a, nullptr);
        a->trigger();
        settle(400);
        QWidget* window = QApplication::activeWindow();
        for (QWidget* top : QApplication::topLevelWidgets())
            if (top != &w && top->isVisible() && top->parent() == &w && !qobject_cast<QDockWidget*>(top)) window = top;
        ASSERT_NE(window, nullptr);
        if (window != &w) {
            window->resize(1200, 800);
            settle(300);
            saveShot(window, prefix + QStringLiteral("-win-") + QLatin1String(name));
            if (uk) collectUntranslated(window, prefix + QStringLiteral("-win-") + QLatin1String(name));
            window->close();
            settle();
        }
    }
    EXPECT_EQ(s.document().undoCount(), undoBefore); // every dialog was cancelled
}

void walkThrough(const QString& language) {
    QTranslator translator;
    const bool uk = language == QLatin1String("uk");
    if (uk) {
        ASSERT_TRUE(translator.load(QStringLiteral("furnitureos_uk"), QStringLiteral(FOS_APP_QM_DIR)));
        QApplication::installTranslator(&translator);
    }
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // A second project file for Import items / Compare with.
    QString other = dir.filePath(QStringLiteral("other.fosp"));
    {
        MainWindow w;
        ASSERT_TRUE(w.newFromTemplate(TemplateKind::WallCabinet, std::nullopt));
        ASSERT_TRUE(w.saveToFile(other));
    }

    for (const SampleProject& sample : sampleProjects()) {
        SCOPED_TRACE(sample.key.toStdString());
        MainWindow w;
        w.resize(1440, 900);
        w.show();
        ASSERT_TRUE(w.openSample(sample.key));
        settle(300);
        Session& s = w.session();
        EXPECT_TRUE(s.validationReport().ok());
        for (const auto& f : s.validationReport().diagnostics)
            if (f.severity == core::Severity::Warning)
                ADD_FAILURE() << sample.key.toStdString() << " warning: " << f.code << ": " << f.message;
        const QString prefix = language + QStringLiteral("-sample-") + sample.key;
        visitWorkspaces(w, prefix, uk);
        undoRedoEdits(w);
        openDialogs(w, prefix, uk, other);
        saveAndReopen(w, dir.filePath(sample.key + QStringLiteral(".fosp")));
    }
    for (TemplateKind kind : allTemplates()) {
        SCOPED_TRACE(templateToken(kind));
        MainWindow w;
        w.resize(1440, 900);
        w.show();
        ASSERT_TRUE(w.newFromTemplate(kind, std::nullopt));
        settle(300);
        EXPECT_TRUE(w.session().validationReport().ok());
        visitWorkspaces(w, language + QStringLiteral("-tpl-") + QLatin1String(templateToken(kind)), uk);
        undoRedoEdits(w);
        saveAndReopen(w, dir.filePath(QLatin1String(templateToken(kind)) + QStringLiteral(".fosp")));
    }
    if (uk) {
        QApplication::removeTranslator(&translator);
        if (!shotDir().isEmpty()) {
            QFile f(QDir(shotDir()).filePath(QStringLiteral("untranslated-uk.txt")));
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&f);
                for (const QString& t : untranslated()) out << t << '\n';
            }
        }
    }
}

TEST(GuiWalkThrough, EverySampleTemplateWorkspaceAndDialogEnglish) { walkThrough(QStringLiteral("en")); }
TEST(GuiWalkThrough, EverySampleTemplateWorkspaceAndDialogUkrainian) { walkThrough(QStringLiteral("uk")); }

} // namespace
} // namespace fos::app::gui
