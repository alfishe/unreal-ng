#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

#include <map>
#include <string>
#include <vector>

#include "debugger/boards/debugboards.h"
#include "emulator/state/statenode.h"

class EmulatorContext;
class QBoxLayout;
class QGroupBox;
class QLabel;
class QProgressBar;
class QTableWidget;
class QToolButton;

/// @file boardpane.h
/// @brief A debugger pane that renders device boards (debugboards.h, debugger-model widget catalog §4): the layout of
/// columns -> groups -> controls every plane reads by `/debug/boards`, `board`, MCP, Lua and Python, drawn by one generic
/// renderer. No device code here: a pane is a list of board ids, and for each the counters of a section of the `gs_stats`
/// board that join it as a group (the NeoGS SD card's command counts, the flash's erases), and optionally one table over
/// a statistics section (the DMA modules).
///
/// Blocks (a board's columns, or one card per board) stand side by side while the pane is wide and stack when it is
/// narrow. Refresh rules as ReportPane (rules.md section 4): a stop refresh marks every value that differs from the last
/// stop; a live refresh shows the values unmarked. The pane never reads by itself: its owner calls refresh with a context
/// it holds a lease on, and carries out a board action (actionRequested) the same way (runAction).
class BoardPane : public QWidget
{
    Q_OBJECT

public:
    struct Part
    {
        std::string board;            ///< the board's id ("neogs", "neogs_sd")
        std::string statsSection;     ///< the `gs_stats` section whose counters join the board as a group ("" none)
        QStringList skipGroups;       ///< the board's groups not shown (the table shows them)
    };
    /// A table over a section of the `gs_stats` board: one row per child (`dma.zx`, `dma.sd`, `dma.mp3`), one column per
    /// member path under it (`starts`, `last.bytes`)
    struct Table
    {
        /// A column whose path ends in `address` shows the 21-bit card address in hex (`#012345`)
        std::string section;
        std::vector<std::pair<std::string, QString>> rows;     ///< child key, row title
        std::vector<std::pair<std::string, QString>> columns;  ///< member path, column title
    };
    enum class Blocks
    {
        Columns,  ///< the boards' columns are the blocks (one board: its layout as the core gives it)
        Cards,    ///< one block per board, titled with the board's title (B3.3: SD · MP3 · Flash)
    };

    BoardPane(std::vector<Part> parts, Blocks blocks, QWidget* parent = nullptr);
    void setTable(Table table);
    /// A widget of the owner's under the boards (the DMA pane's ZX-DMA trace): shown while the boards are; it refreshes
    /// on refreshed()
    void addExtra(QWidget* widget);
    /// A button of the owner's at the end of board `board`'s action row (the SD card's Open media panel, N8): a click
    /// emits uiActionRequested; no board action runs. actionButton(board, id) finds it
    void addUiAction(const QString& board, const QString& id, const QString& title, const QString& tooltip);

    /// Build the boards on `context` and show them; `live`: the machine runs (no change marks)
    void refresh(EmulatorContext* context, bool live);
    /// Forget the last stop's values (another machine)
    void clearMarks() { _lastStop.clear(); }
    /// Carry out a board action (DebugBoards::Action) on `context` and show its answer in the status line
    DebugBoardReply runAction(EmulatorContext* context, const QString& board, const QString& action, const QString& arg);

    /// The text a control shows ("#23", "1,024", "yes"); `type` and `value` as in the board's control
    static QString ValueText(const StateNode& control);

    /// region <For the tests>
    QString status() const;
    /// The last action's answer ("neogs_flash save: done")
    QString answer() const;
    /// The shown text of control `id` of board `board` (empty: not shown)
    QString valueText(const QString& board, const QString& id) const;
    /// The shown group titles of board `board` ("Config", "Paging", ..., the counters' group)
    QStringList groupTitles(const QString& board) const;
    /// The block titles (Cards: the boards' titles)
    QStringList blockTitles() const;
    QTableWidget* table() const { return _table; }
    QToolButton* actionButton(const QString& board, const QString& action) const;
    bool isMarked(const QString& board, const QString& id) const { return _marked.count(board + '/' + id) != 0; }
    bool stacked() const;
    /// endregion

signals:
    /// The user asked for a board action (a button; Insert: the chosen path): the owner calls runAction under its lease
    void actionRequested(const QString& board, const QString& action, const QString& arg);
    /// One of the owner's buttons (addUiAction) was clicked
    void uiActionRequested(const QString& board, const QString& id);
    /// A refresh drew the boards on `context` (still under the caller's lease): the extra widgets refresh with it
    void refreshed(EmulatorContext* context, bool live);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Shown
    {
        QLabel* value = nullptr;          ///< the value's label (a `bar` control: none)
        QProgressBar* bar = nullptr;
        QString text;                     ///< the plain text it shows
    };
    /// The boards to draw (each with its counters group spliced in), in order
    std::vector<StateNode> Collect(EmulatorContext* context);
    void Rebuild(const std::vector<StateNode>& boards);
    void Fill(const std::vector<StateNode>& boards, bool live, std::map<QString, QString>& now);
    void FillTable(bool live, std::map<QString, QString>& now);
    void SetStatus(const QString& text);
    void UpdateDirection();
    bool Changed(const QString& path, const QString& text, bool live);
    static QString Signature(const std::vector<StateNode>& boards);

    std::vector<Part> _parts;
    Blocks _blocks;
    Table _tableSpec;
    QLabel* _status = nullptr;
    QLabel* _answer = nullptr;
    QWidget* _content = nullptr;
    QBoxLayout* _blockLayout = nullptr;
    QTableWidget* _table = nullptr;
    std::vector<QWidget*> _blockWidgets;
    std::vector<QGroupBox*> _groupBoxes;  ///< every group's box, in the boards' order (the title carries the raw register)
    std::vector<QGroupBox*> _cards;       ///< Cards: one per board
    QString _signature;
    StateNode _stats;  ///< the last ready `gs_stats` board (a running machine has one only after a frame end)
    std::map<QString, Shown> _shown;             ///< by "board/control"
    std::map<QString, QStringList> _groups;      ///< the group titles by board
    std::map<QString, QToolButton*> _actions;    ///< by "board/action"
    struct UiAction
    {
        QString board, id, title, tooltip;
    };
    std::vector<UiAction> _uiActions;            ///< addUiAction's, in order
    std::map<QString, QString> _lastStop;
    std::map<QString, bool> _marked;
};
