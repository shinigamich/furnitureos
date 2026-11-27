// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Welcome dialog: shown on the first start and from Help > Welcome. Language choice, a note
/// on units (mm) and the ways to begin: new from template, open a sample project, open a file.
#include "fos/app/samples.hpp"
#include "fos/app/templates.hpp"

#include <QDialog>

class QComboBox;
class QLabel;
class QMenu;
class QPushButton;

namespace fos::app::gui {

/// What the user chose to do in the Welcome dialog.
struct WelcomeChoice {
    enum class Action { None, NewFromTemplate, OpenSample, OpenFile };
    Action action = Action::None;
    TemplateKind templateKind = TemplateKind::BaseCabinet; ///< NewFromTemplate only.
    QString sampleKey;                                     ///< OpenSample only (SampleProject::key).
};

class WelcomeDialog : public QDialog {
    Q_OBJECT
public:
    /// `language`: "en" or "uk", pre-selected. `liveLanguage`: a language change is announced
    /// at once with languageChanged() (start-up, before the main window exists; the receiver
    /// installs the translators and this dialog retranslates itself); otherwise the dialog
    /// notes that the main window changes language after a restart.
    WelcomeDialog(const QString& language, bool liveLanguage, QWidget* parent = nullptr);

    QString language() const; ///< "en" or "uk".
    WelcomeChoice choice() const { return choice_; }

signals:
    void languageChanged(const QString& language);

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void choose(WelcomeChoice choice);

    QString initialLanguage_;
    bool liveLanguage_;
    WelcomeChoice choice_;
    QLabel* heading_;
    QLabel* languageLabel_;
    QComboBox* languageBox_;
    QLabel* restartNote_;
    QLabel* unitsNote_;
    QPushButton* newButton_;
    QPushButton* sampleButton_;
    QPushButton* openButton_;
    QPushButton* closeButton_;
    QMenu* templateMenu_;
    QMenu* sampleMenu_;
};

} // namespace fos::app::gui
