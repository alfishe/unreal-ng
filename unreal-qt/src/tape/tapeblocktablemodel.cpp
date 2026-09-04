/**
 * @file tapeblocktablemodel.cpp
 * @brief Implementation of TapeBlockTableModel (design §9.1/§9.2 — the block
 *        table with play-head marker, consumption greying and the FAST column).
 */

#include "tapeblocktablemodel.h"

#include <algorithm>

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QPalette>

namespace
{
/// Human-readable reject phrase for tooltips (design §9.1 details line).
QString RejectPhrase(FastLoadRejectEnum reason)
{
    switch (reason)
    {
        case FastLoadRejectEnum::None:
            return QStringLiteral("yes — ROM-standard, trap-shaped");
        case FastLoadRejectEnum::NonStandardTiming:
            return QStringLiteral("no — non-standard timing (turbo)");
        case FastLoadRejectEnum::PulseStream:
            return QStringLiteral("no — pulse stream, no byte payload");
        case FastLoadRejectEnum::NonStandardFlag:
            return QStringLiteral("no — non-standard flag byte");
        case FastLoadRejectEnum::ChecksumInvalid:
            return QStringLiteral("no — checksum invalid");
        case FastLoadRejectEnum::Headerless:
            return QStringLiteral("no — headerless");
        case FastLoadRejectEnum::Unplayable:
            return QStringLiteral("no — not playable in this build");
        case FastLoadRejectEnum::ControlFlowInert:
            return QStringLiteral("no — play order not authoritative (control flow)");
        case FastLoadRejectEnum::ControlBlock:
            return QStringLiteral("structural control entry — skipped");
        default:
            return QStringLiteral("no");
    }
}

QString KindDisplay(TapeBlockKindEnum kind)
{
    switch (kind)
    {
        case TapeBlockKindEnum::Header:
            return QStringLiteral("Header");
        case TapeBlockKindEnum::Data:
            return QStringLiteral("Data");
        case TapeBlockKindEnum::Custom:
            return QStringLiteral("Custom");
        case TapeBlockKindEnum::Tone:
            return QStringLiteral("Tone");
        case TapeBlockKindEnum::PulseStream:
            return QStringLiteral("Pulse");
        case TapeBlockKindEnum::Control:
            return QStringLiteral("Control");
        default:
            return QStringLiteral("?");
    }
}
}  // namespace

TapeBlockTableModel::TapeBlockTableModel(QObject* parent) : QAbstractTableModel(parent)
{
}

int TapeBlockTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(_catalog.size());
}

int TapeBlockTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

const TapeBlockDescriptor* TapeBlockTableModel::descriptorAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(_catalog.size()))
    {
        return nullptr;
    }
    return &_catalog[static_cast<size_t>(row)];
}

const TapeBlockDescriptor* TapeBlockTableModel::pairedHeaderAt(int row) const
{
    // r8 pair context: the catalog's pairedHeaderIndex points back at the
    // header whose interpretation the data block carries (Program body, Code
    // body). Bounds-guarded because pairing rides the generation snapshot.
    const TapeBlockDescriptor* descriptor = descriptorAt(row);
    if (!descriptor || descriptor->pairedHeaderIndex == SIZE_MAX ||
        descriptor->pairedHeaderIndex >= _catalog.size())
    {
        return nullptr;
    }
    const TapeBlockDescriptor* header = &_catalog[descriptor->pairedHeaderIndex];
    return header->headerValid ? header : nullptr;
}

std::optional<FastLoadRejectEnum> TapeBlockTableModel::fastLoadRejectAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(_plan.perBlock.size()))
    {
        return std::nullopt;
    }
    return _plan.perBlock[static_cast<size_t>(row)];
}

int TapeBlockTableModel::inFlightRow() const
{
    const bool inFlight = (_state == TapePlaybackState::Playing || _state == TapePlaybackState::Paused);
    if (inFlight && _position.has_value() && _position->blockIndex < _catalog.size())
    {
        return static_cast<int>(_position->blockIndex);
    }
    return -1;
}

bool TapeBlockTableModel::isConsumedRow(int row) const
{
    // The cursor IS the in-flight block during signal playback; everything
    // strictly below it was delivered (signal or fast-load trap).
    return row >= 0 && static_cast<size_t>(row) < _cursor && row != inFlightRow();
}

void TapeBlockTableModel::Rebuild(std::vector<TapeBlockDescriptor> catalog, const TapeFastLoadPlan& plan)
{
    beginResetModel();
    _catalog = std::move(catalog);
    _plan = plan;
    _position.reset();
    _cursor = 0;
    _state = TapePlaybackState::Idle;
    endResetModel();
}

void TapeBlockTableModel::UpdatePosition(TapePlaybackState state, const std::optional<TapePosition>& position, size_t cursor)
{
    const int oldInFlight = inFlightRow();
    const size_t oldCursor = _cursor;

    _state = state;
    _position = position;
    _cursor = cursor;

    // Repaint everything the marker/consumption state can affect: the span
    // between old and new consumption cursor, plus both play-head rows.
    const int newInFlight = inFlightRow();
    const size_t count = _catalog.size();
    const size_t low = std::min(oldCursor, cursor);
    const size_t high = std::max({ oldCursor, cursor, static_cast<size_t>(std::max(oldInFlight, newInFlight) + 1) });

    if (count == 0)
    {
        return;
    }
    const int first = static_cast<int>(std::min(low, count - 1));
    const int last = static_cast<int>(std::min(high == 0 ? 0 : high - 1, count - 1));
    if (first <= last)
    {
        emit dataChanged(index(first, 0), index(last, ColCount - 1),
                         { Qt::DisplayRole, Qt::FontRole, Qt::ForegroundRole, Qt::BackgroundRole, Qt::ToolTipRole });
    }
}

QVariant TapeBlockTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    {
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    switch (section)
    {
        case ColIndex:
            return QStringLiteral("IDX");
        case ColKind:
            return QStringLiteral("KIND");
        case ColHeader:
            return QStringLiteral("HEADER");
        case ColSpeed:
            return QStringLiteral("SPEED");
        case ColFast:
            return QStringLiteral("FAST");
        case ColName:
            return QStringLiteral("NAME");
        case ColType:
            return QStringLiteral("TYPE");
        case ColLength:
            return QStringLiteral("LEN");
        default:
            return QVariant();
    }
}

QVariant TapeBlockTableModel::data(const QModelIndex& index, int role) const
{
    const TapeBlockDescriptor* descriptor = descriptorAt(index.row());
    if (!descriptor || !index.isValid())
    {
        return QVariant();
    }

    const int row = index.row();
    const int column = index.column();
    const int inFlight = inFlightRow();
    const bool inFlightRowNow = (row == inFlight);
    const bool consumed = isConsumedRow(row);

    switch (role)
    {
        case Qt::TextAlignmentRole:
        {
            if (column == ColIndex || column == ColLength)
            {
                return QVariant::fromValue<int>(static_cast<int>(Qt::AlignRight | Qt::AlignVCenter));
            }
            if (column == ColFast)
            {
                return QVariant::fromValue<int>(static_cast<int>(Qt::AlignHCenter | Qt::AlignVCenter));
            }
            break;
        }

        case Qt::FontRole:
        {
            if (inFlightRowNow)
            {
                QFont font;
                font.setBold(true);
                return font;
            }
            break;
        }

        case Qt::ForegroundRole:
        {
            // Consumed blocks fade out; the play head keeps normal (bold) text
            if (consumed)
            {
                return QBrush(QColor(140, 140, 140));
            }
            break;
        }

        case Qt::BackgroundRole:
        {
            if (inFlightRowNow)
            {
                QColor highlight = QPalette().color(QPalette::Highlight);
                highlight.setAlpha(60);
                return QBrush(highlight);
            }
            break;
        }

        case Qt::ToolTipRole:
        {
            switch (column)
            {
                case ColIndex:
                    if (inFlightRowNow)
                    {
                        return QStringLiteral("In flight — play head");
                    }
                    return consumed ? QStringLiteral("Consumed (signal or fast-load trap)")
                                    : QStringLiteral("Not yet consumed");
                case ColFast:
                {
                    const auto reject = fastLoadRejectAt(row);
                    if (!reject.has_value())
                    {
                        return QVariant();
                    }
                    return QStringLiteral("Fast load: ") + RejectPhrase(reject.value());
                }
                case ColHeader:
                {
                    if (descriptor->pairedHeaderIndex != SIZE_MAX)
                    {
                        return QStringLiteral("Pairs with header #%1").arg(descriptor->pairedHeaderIndex);
                    }
                    if (descriptor->pairedDataIndex != SIZE_MAX)
                    {
                        return QStringLiteral("Followed by data block #%1").arg(descriptor->pairedDataIndex);
                    }
                    if ((descriptor->kind == TapeBlockKindEnum::Data || descriptor->kind == TapeBlockKindEnum::Custom) &&
                        descriptor->headerless)
                    {
                        return QStringLiteral(
                            "Headerless — no valid ROM header directly precedes this block (custom-loader payload)");
                    }
                    break;
                }
                default:
                    break;
            }
            // Fall through for all columns: the common summary tooltip
            const char* profile = getTapeSpeedProfileName(descriptor->timing.profile);
            const QString checksum = (descriptor->rawSize == 0)
                                         ? QStringLiteral("n/a")
                                         : (descriptor->checksumValid ? QStringLiteral("OK") : QStringLiteral("INVALID"));
            return QStringLiteral("Block %1 — %2, %3, checksum %4, ~%5s")
                .arg(row)
                .arg(KindDisplay(descriptor->kind))
                .arg(QString::fromUtf8(profile))
                .arg(checksum)
                .arg(descriptor->estimatedSeconds, 0, 'f', 1);
        }

        case Qt::DisplayRole:
        {
            switch (column)
            {
                case ColIndex:
                    if (inFlightRowNow)
                    {
                        return QStringLiteral("▸ %1").arg(row);
                    }
                    return row;
                case ColKind:
                    return KindDisplay(descriptor->kind);
                case ColHeader:
                    // Inverted r10: YES marks the blocks that DO carry a
                    // header — paired data bodies. Headerless custom-loader
                    // payloads (the old YES) are the "no" rows now.
                    if (descriptor->kind == TapeBlockKindEnum::Data || descriptor->kind == TapeBlockKindEnum::Custom)
                    {
                        return descriptor->headerless ? QStringLiteral("no") : QStringLiteral("YES");
                    }
                    return QStringLiteral("—");
                case ColSpeed:
                {
                    QString speed;
                    switch (descriptor->timing.profile)
                    {
                        case TapeSpeedProfileEnum::StandardRom:
                            speed = QStringLiteral("Std");
                            break;
                        case TapeSpeedProfileEnum::Custom:
                            speed = QStringLiteral("Turbo");
                            break;
                        case TapeSpeedProfileEnum::PulseStream:
                            speed = QStringLiteral("Pulse");
                            break;
                        default:
                            speed = QStringLiteral("?");
                            break;
                    }
                    if (descriptor->baudEstimate > 0)
                    {
                        speed += QStringLiteral(" %1 bps").arg(descriptor->baudEstimate);
                    }
                    return speed;
                }
                case ColFast:
                {
                    const auto reject = fastLoadRejectAt(row);
                    if (!reject.has_value())
                    {
                        return QVariant();
                    }
                    return (reject.value() == FastLoadRejectEnum::None) ? QStringLiteral("⚡") : QStringLiteral("·");
                }
                case ColName:
                {
                    // r8: a paired data block shows its header's name — the
                    // BASIC/Code body is the same logical unit as the header
                    if (!descriptor->name.empty())
                    {
                        return QString::fromStdString(descriptor->name);
                    }
                    const TapeBlockDescriptor* paired = pairedHeaderAt(row);
                    if (paired && !paired->name.empty())
                    {
                        return QString::fromStdString(paired->name);
                    }
                    return QVariant();
                }
                case ColType:
                {
                    if (descriptor->headerValid)
                    {
                        return QString::fromUtf8(getTapeBlockTypeName(descriptor->headerType));
                    }
                    // r8: same propagation as NAME — "Program" on the data
                    // row makes the BASIC loader block legible in the table
                    const TapeBlockDescriptor* paired = pairedHeaderAt(row);
                    if (paired)
                    {
                        return QString::fromUtf8(getTapeBlockTypeName(paired->headerType));
                    }
                    // r11: a headerless payload is raw bytes by definition —
                    // the conventional TYPE reading is Code (custom-loader
                    // machine code), never a blank cell
                    if (descriptor->headerless)
                    {
                        return QString::fromUtf8(getTapeBlockTypeName(TAP_BLOCK_CODE));
                    }
                    return QVariant();
                }
                case ColLength:
                    return descriptor->rawSize > 0 ? QVariant::fromValue<qulonglong>(descriptor->rawSize) : QVariant();
                default:
                    return QVariant();
            }
        }

        default:
            break;
    }

    return QVariant();
}
