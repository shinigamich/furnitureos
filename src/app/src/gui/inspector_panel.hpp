// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
/// \file
/// Properties inspector. Cabinet: parameters (grouped by kind, editable as numbers or
/// expressions), construction options, materials by role. Panel item: parameters. Part:
/// read-only facts from the resolved model. Every edit is a project::Document command;
/// errors are shown inline with the engine's message and change nothing.
#include "fos/app/parameter_rows.hpp"

#include <QLabel>
#include <QPointer>
#include <QScrollArea>

#include <map>
#include <string>

class QLineEdit;
class QVBoxLayout;

namespace fos::app {
class Session;
}

namespace fos::app::gui {

class InspectorPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit InspectorPanel(Session& session, QWidget* parent = nullptr);

    /// Types `text` into the editor of parameter `name` and commits it, exactly as the user
    /// would (Enter). Returns false when no such editor is shown or the edit was refused.
    bool submitParameter(const QString& name, const QString& text);
    /// Inline error currently shown under parameter `name` (empty = none).
    QString parameterError(const QString& name) const;

private:
    void rebuild();
    void buildCabinet(QVBoxLayout* layout, const std::string& itemId);
    void buildPanelItem(QVBoxLayout* layout, const std::string& itemId);
    void buildPart(QVBoxLayout* layout, const std::string& partId);
    void buildParameters(QVBoxLayout* layout, const std::string& itemId, const std::vector<ParameterGroup>& groups,
                         bool cabinet);
    void buildConstruction(QVBoxLayout* layout, const std::string& itemId);
    void buildMaterials(QVBoxLayout* layout, const std::string& itemId);
    void buildNameEditor(QVBoxLayout* layout, const std::string& itemId);
    bool commitParameter(const std::string& itemId, const std::string& name, QLineEdit* editor);
    /// Runs the command; on error shows it in `errorLabel` and returns false.
    bool run(const project::Command& command, QLabel* errorLabel);

    struct Editor {
        QPointer<QLineEdit> edit;
        QPointer<QLabel> error;
    };

    Session& session_;
    QString filter_;
    std::map<std::string, Editor> editors_;
};

/// Red, word-wrapped label for inline error messages (hidden while empty).
QLabel* makeErrorLabel(QWidget* parent);
void showError(QLabel* label, const QString& message);

} // namespace fos::app::gui
