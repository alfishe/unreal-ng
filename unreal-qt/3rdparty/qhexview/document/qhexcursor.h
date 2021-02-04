#ifndef QHEXCURSOR_H
#define QHEXCURSOR_H

#include <QObject>

#define DEFAULT_HEX_LINE_LENGTH      0x10
#define DEFAULT_AREA_IDENTATION      0x01

struct QHexPosition
{
    quint64 line;           // Line number (using lineWidth formatting)
    quint8 column;          // Column in line
    quint8 nibbleindex;     // Half-byte(tetrade) index. Can be 0 - no tetrade, 1 or 2.
    quint8 lineWidth;

    QHexPosition() = default;
    QHexPosition(const QHexPosition&) = default;

    QHexPosition& operator= (const QHexPosition& rhs)
    {
        line = rhs.line;
        column = rhs.column;
        nibbleindex = rhs.nibbleindex;

        return *this;
    }

    void setOffset(quint64 offset)
    {
        line = offset / lineWidth;
        column = static_cast<quint8>(offset % lineWidth);
    }

    quint64 offset() const
    {
        quint64 result = static_cast<quint64>(line * lineWidth) + column;

        return result;
    }

    quint64 operator-(const QHexPosition& rhs) const { return this->offset() - rhs.offset(); }
    bool operator==(const QHexPosition& rhs) const { return (line == rhs.line) && (column == rhs.column) && (nibbleindex == rhs.nibbleindex); }
    bool operator!=(const QHexPosition& rhs) const { return (line != rhs.line) || (column != rhs.column) || (nibbleindex != rhs.nibbleindex); }
};

class QHexCursor : public QObject
{
    Q_OBJECT

    public:
        enum InsertionMode { OverwriteMode, InsertMode };

    public:
        explicit QHexCursor(QObject *parent = nullptr);

    public:
        const QHexPosition& selectionStart() const;
        const QHexPosition& selectionEnd() const;
        const QHexPosition& position() const;
        InsertionMode insertionMode() const;
        int selectionLength() const;
        quint64 currentLine() const;
        qint8 currentColumn() const;
        int currentNibble() const;
        quint64 selectionLine() const;
        qint8 selectionColumn() const;
        int selectionNibble() const;
        bool atEnd() const;
        bool isLineSelected(quint64 line) const;
        bool hasSelection() const;
        void clearSelection();

    public:
        void moveTo(const QHexPosition& pos);
        void moveTo(quint64 line, quint8 column, int nibbleindex = 1);
        void moveTo(quint64 offset);
        void select(const QHexPosition& pos);
        void select(quint8 lineOffset);
        void select(quint64 line, quint8 column, int nibbleindex = 1);
        void select(int length);
        void selectOffset(quint64 offset, int length);
        void setInsertionMode(InsertionMode mode);
        void setLineWidth(quint8 width);
        void switchInsertionMode();

    signals:
        void positionChanged();
        void insertionModeChanged();

    private:
        InsertionMode m_insertionmode;
        quint8 m_lineWidth;
        QHexPosition m_position;
        QHexPosition m_selection;
};

#endif // QHEXCURSOR_H
