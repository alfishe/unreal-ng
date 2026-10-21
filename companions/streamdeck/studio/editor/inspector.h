// Properties of the selected node (or of the scenario when nothing is selected). Every edit is one undo step;
// the JSON box at the bottom edits anything the form does not show (dials, multi steps, toggle actions ...).
#pragma once
#include <QStringList>
#include <QWidget>
#include <nlohmann/json.hpp>
#include <string>

// The action kind box: the engine's kinds (sdb::scn::ActionKinds), and what a node's "do" becomes when one is picked.
[[nodiscard]] QStringList ActionKindItems();
[[nodiscard]] nlohmann::json ActionTemplate(const std::string& kind);

class EditorDocument;
class QPlainTextEdit;
class QLabel;

class Inspector : public QWidget
{
    Q_OBJECT
public:
    explicit Inspector(EditorDocument* doc, QWidget* parent = nullptr);
    void Rebuild();

signals:
    void Message(const QString& text);
    void OpenRequested(const QString& file);
    void InlineRequested(const QString& id);

private:
    void BuildNode(const QString& id);
    void BuildScenario();
    void BuildInclude(const QString& id);
    void BuildIncluded(const QString& id);
    bool Patch(const QString& text, const nlohmann::json& p);

    EditorDocument* _doc;
    QWidget* _form = nullptr;
    QLabel* _title;
    QPlainTextEdit* _json;
    QString _shownId;
};
