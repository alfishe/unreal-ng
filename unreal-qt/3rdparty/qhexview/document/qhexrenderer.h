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

signals:


    /// region <Event handlers / Slots>
public slots:
    void onDocumentChanged();
    void onLayoutChanged();
    /// endregion <Event handlers / Slots>

private:
    void recalculateRenderParameters();

private:
    QString hexString(quint64 line, QByteArray *rawline = nullptr) const;
    QString asciiString(quint64 line, QByteArray *rawline = nullptr) const;
    QByteArray getLine(quint64 line) const;
    quint64 rendererLength() const;
    quint64 getAddressWidthSymbols() const;
    quint64 getHexColumnX() const;
    quint64 getAsciiColumnX() const;
    quint64 getEndColumnX() const;
    quint64 getCellWidth() const;
    quint64 getNibbleIndex(int line, int relx) const;
    void unprintableChars(QByteArray &ascii) const;

private:
    void renderDocumentStyles(QPainter* painter, QTextDocument *textdocument) const;
    void renderBasicStyle(QTextCursor& textcursor, const QByteArray& rawline, int factor = 1) const;
    void renderMetadata(QTextCursor& textcursor, quint64 line, int factor = 1) const;
    void renderSelectionAscii(QTextCursor& textcursor, quint64 line) const;
    void renderSelectionHex(QTextCursor& textcursor, quint64 line) const;
    void renderCursorAscii(QTextCursor& textcursor, quint64 line) const;
    void renderCursorHex(QTextCursor& textcursor, quint64 line) const;

    void drawAddress(QPainter *painter, const QPalette &palette, const QRect &linerect, quint64 line);
    void drawHex(QPainter *painter, const QPalette &palette, const QRect &linerect, quint64 line);
    void drawAscii(QPainter *painter, const QPalette &palette, const QRect &linerect, quint64 line);
    void drawHeader(QPainter *painter, const QPalette &palette);

private:
    QHexDocument* m_document;
    QFontMetrics m_fontmetrics;
    QHexRenderer::AreaTypeEnum m_selectedArea;
    bool m_cursorenabled;

    // Pre-calculated values. Kept until data or layout changed
    quint64 m_borderSize;
    quint64 m_cellWidth;
    quint64 m_addressWidthSymbols;
    quint8 m_hexLineWidthSymbols;

    quint64 m_addressColumnWidth;
    quint64 m_hexColumnWidth;
    quint64 n_asciiColumnWidth;

    quint64 m_addressColumnX;
    quint64 m_hexColumnX;
    quint64 m_asciiColumnX;
    quint64 m_endColumnX;

};

#endif // QHEXRENDERER_H
