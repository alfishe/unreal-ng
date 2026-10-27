#pragma once

/// @file panefocus.h
/// @brief The debugger window's panes as a focus ring for controllers (debug-and-ttd.md §3.3): the next / previous
/// pane takes the focus with a frame around it, and the cursor keys go to the pane in focus. A pane of value cells
/// (registers, stack) gets a cursor of its own: the arrows move it to the nearest cell that way, Enter acts as a
/// double click on the cell (the memory view follows its value). The keyboard drives the same cursor.

#include <QObject>
#include <QString>
#include <QWidget>

#include <vector>

class QEvent;

class PaneFocus : public QObject
{
    Q_OBJECT
public:
    struct Pane
    {
        QString name;
        QWidget* widget = nullptr;
        std::vector<QWidget*> cells;  ///< value cells with a doubleClicked() signal; empty: keys go to the widget
    };

    PaneFocus(QWidget* window, std::vector<Pane> panes);

    /// Focus the pane @p delta places along the ring from the one in focus
    void Move(int delta);
    /// Focus the pane at @p index of the ring (0 disassembly, 1 registers, 2 memory, 3 stack in the debugger)
    void Focus(int index);
    /// Qt::Key_Up / Down / Left / Right / PageUp / PageDown / Return into the pane in focus
    void Key(int key);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    class Frame;

    int Focused() const;  ///< the pane holding the window's focus, -1 when none
    QWidget* Receiver(const Pane& pane) const;
    bool CellKey(Pane& pane, size_t index, int key);
    void Place();

    QWidget* _window;
    std::vector<Pane> _panes;
    std::vector<size_t> _cell;  ///< the cursor in each pane of cells
    int _pane = 0;              ///< the pane focused last
    Frame* _paneFrame;
    Frame* _cellFrame;
};
