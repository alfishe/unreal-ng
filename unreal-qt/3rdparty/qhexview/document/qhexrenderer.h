#ifndef QHEXRENDERER_H
#define QHEXRENDERER_H

/*
 * Nibble encoding:
 *           AB -> [A][B]
 * Nibble Index:    1  0
 */

#include <QTextDocument>
#include <QPainter>
#include "qhexdocument.h"

class QHexRenderer : public QObject
{
    Q_OBJECT

    public:
        enum AreaTypeEnum { HeaderArea, AddressArea, HexArea, AsciiArea, ExtraArea };

    public:
        explicit QHexRenderer(QHexDocument* document, const QFontMetrics& fontmetrics, QObject *parent = nullptr);
        void renderFrame(QPainter* painter);
        void render(QPainter* painter, quint64 startLine, quint64 endLine, quint64 firstline);  // begin included, end excluded
        void enableCursor(bool b = true);
        void selectArea(const QPoint& pt);

    public:
        void blinkCursor();
        bool hitTest(const QPoint& pt, QHexPosition* position, quint64 firstline) const;
        QHexRenderer::AreaTypeEnum hitDetectArea(const QPoint& pt) const;
        QHexRenderer::AreaTypeEnum selectedArea() const;
        bool editableArea(QHexRenderer::AreaTypeEnum area) const;
        quint64 documentLastLine() const;
        quint8 documentLastColumn() const;
        quint64 documentLines() const;
        quint64 documentWidth() const;
        quint64 lineHeight() const;
        QRect getLineRect(quint64 line, quint64 firstline) const;
        quint64 headerLineCount() const;
        quint64 borderSize() const;
        quint8 hexLineWidth() const;

    private:
        QString hexString(quint64 line, QByteArray *rawline = nullptr) const;
        QString asciiString(quint64 line, QByteArray *rawline = nullptr) const;
        QByteArray getLine(quint64 line) const;
        quint64 rendererLength() const;
        quint64 getAddressWidth() const;
        quint64 getHexColumnX() const;
        quint64 getAsciiColumnX() const;
        quint64 getEndColumnX() const;
        quint64 getCellWidth() const;
        quint64 getNibbleIndex(int line, int relx) const;
        void unprintableChars(QByteArray &ascii) const;

    private:
        void applyDocumentStyles(QPainter* painter, QTextDocument *textdocument) const;
        void applyBasicStyle(QTextCursor& textcursor, const QByteArray& rawline, int factor = 1) const;
        void applyMetadata(QTextCursor& textcursor, quint64 line, int factor = 1) const;
        void applySelection(QTextCursor& textcursor, quint64 line, int factor = 1) const;
        void applyCursorAscii(QTextCursor& textcursor, quint64 line) const;
        void applyCursorHex(QTextCursor& textcursor, quint64 line) const;
        void drawAddress(QPainter *painter, const QPalette &palette, const QRect &linerect, quint64 line);
        void drawHex(QPainter *painter, const QPalette &palette, const QRect &linerect, quint64 line);
        void drawAscii(QPainter *painter, const QPalette &palette, const QRect &linerect, quint64 line);
        void drawHeader(QPainter *painter, const QPalette &palette);

    private:
        QHexDocument* m_document;
        QFontMetrics m_fontmetrics;
        QHexRenderer::AreaTypeEnum m_selectedArea;
        bool m_cursorenabled;
};

#endif // QHEXRENDERER_H
