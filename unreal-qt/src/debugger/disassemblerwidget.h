#pragma once

#include <QAbstractItemView>
#include <QHeaderView>
#include <QMouseEvent>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>
#include <unordered_map>

#include "disassemblertablemodel.h"

QT_BEGIN_NAMESPACE
namespace Ui
{
class DisassemblerWidget;
}
QT_END_NAMESPACE

class Emulator;

class DisassemblerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DisassemblerWidget(QWidget* parent = nullptr);
    virtual ~DisassemblerWidget() override;

    // Virtual table interface
    void setDisassemblerAddress(uint16_t pc);
    void setEmulator(Emulator* emulator);
    Emulator* emulator() const
    {
        return m_emulator;
    }

    // Getter for current PC
    uint16_t getCurrentPC() const
    {
        return m_currentPC;
    }

public slots:
    void reset();
    void refresh();
    void goToAddress(uint16_t address);

signals:
    void addressSelected(uint16_t address);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onCurrentRowChanged(const QModelIndex& current, const QModelIndex& previous);

private:
    void initializeTable();
    void updateVisibleRange();
    QString formatAddress(uint16_t address) const;
    void onResizeTimer();

    Ui::DisassemblerWidget* ui;
    DisassemblerTableModel* m_model;
    Emulator* m_emulator;
    uint16_t m_displayAddress;
    uint16_t m_currentPC;
    bool m_updatingView;
    bool m_isDragging;
    QPoint m_dragStartPos;

    // Resize optimization
    QTimer m_resizeTimer;
    QSize m_lastSize;

    enum class ScrollMode
    {
        Command,
        Line
    };

    ScrollMode m_scrollMode;

    static constexpr uint16_t MAX_ADDRESS = 0xFFFF;
    static constexpr int CACHE_MARGIN = 5;
};
