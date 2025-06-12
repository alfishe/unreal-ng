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
    /// 256 bytes before and 256 bytes after PC address will be disassembled
    constexpr static const size_t DISASSEMBLY_RANGE = 0x100;

public:
    explicit DisassemblerTableModel(Emulator* emulator, QObject *parent = nullptr);
    ~DisassemblerTableModel() override;

    void refresh();
    void reset();

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // Custom methods
    void setEmulator(Emulator* emulator);
    void setVisibleRange(uint16_t start, uint16_t end);
    void setCurrentPC(uint16_t pc);
    void loadDisassemblyRange(uint16_t start, uint16_t end, std::optional<uint16_t> pc = std::nullopt);

    // Find row by address
    int findRowForAddress(uint16_t address) const;

    // Debug method
    void dumpState() const;

signals:
    // Signal emitted when the current PC changes
    void currentPCChanged(uint16_t newPC);

private:
    // Helper methods for disassembly
    void disassembleForward(uint16_t start, uint16_t end);
    void disassembleBackward(uint16_t pc, uint16_t end);
    DecodedInstruction disassembleAt(uint16_t addr, class Z80Disassembler* disassembler, class Memory* memory);

    Emulator* m_emulator;
    std::map<uint16_t, DecodedInstruction> m_decodedInstructionsCache;
    uint16_t m_currentPC;
    uint16_t m_visibleStart;
    uint16_t m_visibleEnd;
    QVector<QString> m_headers;
};
