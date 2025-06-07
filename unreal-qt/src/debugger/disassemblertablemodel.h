#pragma once

#include <QAbstractTableModel>
#include <QVector>
#include <QMap>
#include <QString>
#include <QVariant>
#include <QModelIndex>
#include <QObject>
#include <cstdint>

// Forward declarations
class Emulator;
class DecodedInstruction;

class DisassemblerTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit DisassemblerTableModel(Emulator* emulator, QObject *parent = nullptr);
    ~DisassemblerTableModel() override;

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        qDebug() << "Data requested for row:" << index.row() << "col:" << index.column() << "role:" << role;
        return data_impl(index, role);
    };
    QVariant data_impl(const QModelIndex& index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // Custom methods
    void setEmulator(Emulator* emulator);
    void setVisibleRange(uint16_t start, uint16_t end);
    void setCurrentPC(uint16_t pc);
    void loadDisassemblyRange(uint16_t start, uint16_t end);
    void refresh();
    void reset();
    void setHorizontalHeaderLabels(const QStringList& headers);
    
    // Debug method
    void dumpState() const;

private:
    Emulator* m_emulator;
    QMap<uint16_t, DecodedInstruction> m_instructions;
    uint16_t m_startAddress;
    uint16_t m_endAddress;
    uint16_t m_currentPC;
    uint16_t m_visibleStart;
    uint16_t m_visibleEnd;
    QVector<QString> m_headers;

    void ensureRangeLoaded(uint16_t start, uint16_t end);
};
