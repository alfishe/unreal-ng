/**
 * @file tapeblocktablemodel.h
 * @brief TapeBlockTableModel — QAbstractTableModel over a COPIED catalog
 *        (design §9.2).
 *
 * Rows arrive via Rebuild() on catalog-generation change; the live play-head
 * marker and consumption greying arrive via UpdatePosition() on every
 * coalesced snapshot tick. The model only ever sees copies — no core object
 * is touched from here.
 */

#pragma once

#include <QAbstractTableModel>

#include <optional>
#include <vector>

#include "emulator/io/tape/tapecatalog.h"  // TapeFastLoadPlan, FastLoadRejectEnum
#include "emulator/io/tape/tape.h"         // TapePlaybackState, TapePosition
#include "emulator/io/tape/tapetypes.h"    // TapeBlockDescriptor

class TapeBlockTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        ColIndex = 0,    // block index + in-flight play-head marker
        ColKind,         // Header / Data / Custom / Tone / Pulse / Control
        ColHeader,       // "YES" (paired header) / "no" (headerless) / "—"
        ColSpeed,        // "Std 1365 bps" / "Turbo 3465 bps" / "Pulse"
        ColFast,         // "⚡" trap-shaped / "·" signal path
        ColName,         // header-interpreted name (or the paired header's — r8)
        ColType,         // Program / Code / arrays (or the paired header's — r8)
        ColLength,       // rawSize
        ColCount
    };

    explicit TapeBlockTableModel(QObject* parent = nullptr);

    // QAbstractTableModel
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    /// Full rebuild on catalog-generation change: swaps the copied descriptors
    /// and the advisory fast-load plan (per-block FAST column source).
    void Rebuild(std::vector<TapeBlockDescriptor> catalog, const TapeFastLoadPlan& plan);

    /// Per-tick marker update: consumed rows grey out, the in-flight block
    /// gets the "▸" play-head marker, bold text and a highlight background.
    void UpdatePosition(TapePlaybackState state, const std::optional<TapePosition>& position, size_t cursor);

    /// Descriptor backing `row` (nullptr when out of range) — details-pane input.
    const TapeBlockDescriptor* descriptorAt(int row) const;

    /// Valid header descriptor paired with `row`'s data block (nullptr when the
    /// block is headerless, unpaired or the index is out of range) — r8 pair
    /// context for the NAME/TYPE columns and the details pane.
    const TapeBlockDescriptor* pairedHeaderAt(int row) const;

    /// Plan reject classification for `row` (nullopt when no plan entry).
    std::optional<FastLoadRejectEnum> fastLoadRejectAt(int row) const;

private:
    int inFlightRow() const;  // -1 when nothing is in flight
    bool isConsumedRow(int row) const;

    std::vector<TapeBlockDescriptor> _catalog;
    TapeFastLoadPlan _plan;
    TapePlaybackState _state = TapePlaybackState::Idle;
    std::optional<TapePosition> _position;
    size_t _cursor = 0;
};
