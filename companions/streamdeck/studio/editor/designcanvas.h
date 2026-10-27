// The editor's canvas: the menu being edited on the model's real panel, faces drawn by the same engine the runtime
// uses. Click = select, double-click a menu = enter (the back key = up), drag keys to move / swap / into a menu,
// drop palette presets on keys, right-click for the rest.
#pragma once
#include <QPoint>
#include <map>
#include <string>

#include "../deckwidget.h"

class EditorDocument;
class QMimeData;

class DesignCanvas : public DeckWidget
{
    Q_OBJECT
public:
    explicit DesignCanvas(EditorDocument* doc, QWidget* parent = nullptr);
    void Refresh();
    // The icon pack the preview draws glyph faces from ("" = the scenario's settings.pack, else magicwb)
    void SetPack(const std::string& pack);
    [[nodiscard]] const std::string& Pack() const
    {
        return _pack;
    }
    [[nodiscard]] QImage RenderImage(double scale) const;  // the canvas as shown (for screenshots)
    [[nodiscard]] int SelectedSlotKey() const              // an empty key clicked last (-1 none)
    {
        return _slotKey;
    }

signals:
    void Message(const QString& text);        // errors / hints for the status line
    void ExportRequested(const QString& id);  // "Export subtree..." / "Move to file (link)..."
    void LinkRequested(const QString& id);
    void ImportRequested(int key, const QString& mode);  // "Import file ..." at a key (menu | items | link)
    void InlineRequested(const QString& id);             // an include -> an editable copy

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override {}
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dragLeaveEvent(QDragLeaveEvent*) override;
    void dropEvent(QDropEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;

private:
    [[nodiscard]] std::string IdAt(int key) const;
    [[nodiscard]] bool IsMenu(const std::string& id) const;
    void DropOn(int key, const QMimeData* mime);

    EditorDocument* _doc;
    std::map<int, std::string> _keyIds;  // key -> node id on the shown menu
    std::map<int, bool> _keyErrors;
    int _backKey = -1, _dropKey = -1, _pressKey = -1, _slotKey = -1;
    QPoint _pressPos;
    std::string _pack;
};
