#include "qhexrenderer.h"
#include <QApplication>
#include <QTextCursor>
#include <QWidget>
#include <cctype>
#include <cmath>

#define HEX_UNPRINTABLE_CHAR '.'

/// region <Constructors / Destructors>

QHexRenderer::QHexRenderer(QHexDocument* document, const QFontMetrics &fontmetrics, QObject *parent) : QObject(parent), m_document(document), m_fontmetrics(fontmetrics)
{
    m_selectedArea = QHexRenderer::HexArea;
    m_cursorenabled = false;

    // Subscribe to document / layout changes
    connect(m_document, SIGNAL(documentChanged()), this, SLOT(onDocumentChanged()));
    connect(parent, SIGNAL(layoutChanged()), this, SLOT(onLayoutChanged()));
}

/// endregion </Constructors / Destructors>

/// Draw column delimiter lines / frames based on each column widths
/// \param painter
void QHexRenderer::renderFrame(QPainter *painter)
{
    QRect rect = painter->window();
    int hexx = this->getHexColumnX();
    int asciix = this->getAsciiColumnX();
    int endx = this->getEndColumnX();

    painter->drawLine(0,
                      headerLineCount() * lineHeight() - 1,
                      endx,
                      headerLineCount() * lineHeight() - 1);

    painter->drawLine(hexx,
                      rect.top(),
                      hexx,
                      rect.bottom());

    painter->drawLine(asciix,
                      rect.top(),
                      asciix,
                      rect.bottom());

    painter->drawLine(endx,
                      rect.top(),
                      endx,
                      rect.bottom());
}

/// Render content line by line
/// \param painter
/// \param startLine
/// \param endLine
/// \param firstline Index of first visible line in viewport (offset)
void QHexRenderer::render(QPainter *painter, quint64 startLine, quint64 endLine, quint64 firstline)
{
    QPalette palette = qApp->palette();

    // Render view header titles
    drawHeader(painter, palette);

    // Render data records
    quint64 documentLines = this->documentLines();
    for (quint64 line = startLine; line < std::min(endLine, documentLines); line++)
    {
        QRect lineRect = this->getLineRect(line, firstline);

        // Draw stripes on the background to distinct even and odd lines
        if (line % 2)
            painter->fillRect(lineRect, palette.brush(QPalette::Window));
        else
            painter->fillRect(lineRect, palette.brush(QPalette::Base));

        this->drawAddress(painter, palette, lineRect, line);
        this->drawHex(painter, palette, lineRect, line);
        this->drawAscii(painter, palette, lineRect, line);
    }
}

void QHexRenderer::enableCursor(bool b)
{
    m_cursorenabled = b;
}

void QHexRenderer::selectArea(const QPoint &pt)
{
    QHexRenderer::AreaTypeEnum area = this->hitDetectArea(pt);

    if (editableArea(area))
    {
        m_selectedArea = area;
    }
}

///
/// \param pt
/// \param position
/// \param firstline First visible line index in viewport (line offset)
/// \return
bool QHexRenderer::hitTest(const QPoint &pt, QHexPosition *position, quint64 firstline) const
{
    // 1. Determine area type
    QHexRenderer::AreaTypeEnum area = this->hitDetectArea(pt);
    if (!editableArea(area))
        return false;

    // 2. Calculate line, column positions within editable area
    position->line = std::min(firstline + (pt.y() / this->lineHeight()) - headerLineCount(), this->documentLastLine());
    position->lineWidth = this->hexLineWidth();

    if (area == QHexRenderer::HexArea)  // Mouse cursor is within hex area
    {
        int relx = pt.x() - this->getHexColumnX() - this->borderSize();
        quint8 symbolPositionX = ceil(relx / this->getCellWidth());
        position->column = ceil(symbolPositionX / 3);
        // First half-byte/tetrade/nibble has index 1, second - 0
        // 0xFA: F - 1, A - 0
        position->nibbleindex = (symbolPositionX % 3 == 0) ? 1 : 0;
     }
    else                                // Mouse cursor is within ASCII area
    {
        int relx = pt.x() - this->getAsciiColumnX() - this->borderSize();
        position->column = ceil(relx / this->getCellWidth());
        position->nibbleindex = 1;
    }

    if (position->line == this->documentLastLine())
    {
        // For very last document line - ensure that last column is not exceeding document end
        QByteArray ba = this->getLine(position->line);
        position->column = std::min(position->column, static_cast<quint8>(ba.length()));
    }
    else
    {
        // For every other line it's simple
        position->column = std::min(position->column, static_cast<quint8>(hexLineWidth() - 1));
    }

    return true;
}

QHexRenderer::AreaTypeEnum QHexRenderer::hitDetectArea(const QPoint &pt) const
{
    if (pt.y() < headerLineCount() * lineHeight())
        return QHexRenderer::HeaderArea;

    if ((pt.x() >= this->borderSize()) && (pt.x() <= (this->getHexColumnX() - this->borderSize())))
        return QHexRenderer::AddressArea;

    if ((pt.x() > (this->getHexColumnX() + this->borderSize())) && (pt.x() < (this->getAsciiColumnX() - this->borderSize())))
        return QHexRenderer::HexArea;

    if ((pt.x() > (this->getAsciiColumnX() + this->borderSize())) && (pt.x() < (this->getEndColumnX() - this->borderSize())))
        return QHexRenderer::AsciiArea;

    return QHexRenderer::ExtraArea;
}

QHexRenderer::AreaTypeEnum QHexRenderer::selectedArea() const
{
    return m_selectedArea;
}

bool QHexRenderer::editableArea(QHexRenderer::AreaTypeEnum area) const
{
    return (area == QHexRenderer::HexArea || area == QHexRenderer::AsciiArea);
}

quint64 QHexRenderer::documentLastLine() const
{
    return this->documentLines() - 1;
}

quint8 QHexRenderer::documentLastColumn() const
{
    return this->getLine(this->documentLastLine()).length();
}

quint64 QHexRenderer::documentLines() const
{
    return m_document->dataLines();
}

quint64 QHexRenderer::documentWidth() const
{
    return this->getEndColumnX();
}

quint64 QHexRenderer::lineHeight() const
{
    return m_fontmetrics.height();
}

QRect QHexRenderer::getLineRect(quint64 line, quint64 firstline) const
{
    QRect rect = QRect(0, static_cast<int>((line - firstline + headerLineCount()) * lineHeight()), this->getEndColumnX(), lineHeight());
    return rect;
}

quint64 QHexRenderer::headerLineCount() const
{
    return 1;
}

quint64 QHexRenderer::borderSize() const
{
    return m_borderSize;
}

quint8 QHexRenderer::hexLineWidth() const
{
    return m_hexLineWidthSymbols;
}

/// region <Event handlers / Slots>

void QHexRenderer::onDocumentChanged()
{
    recalculateRenderParameters();
}

void QHexRenderer::onLayoutChanged()
{
    recalculateRenderParameters();
}

/// endregion <Event handlers / Slots>

void QHexRenderer::recalculateRenderParameters()
{
    /// region <Cell width>
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    // Do not rely that we have monospace font, get average from all font symbols
    m_cellWidth = static_cast<quint64>(m_fontmetrics.averageCharWidth());
#else
    m_cellWidth = m_fontmetrics.width(" ");
#endif
    /// endregion </Cell width>

    /// region <Border size>

    m_borderSize = DEFAULT_AREA_IDENTATION * this->getCellWidth();

    if (m_document)
    {
        m_borderSize = m_document->areaIdent() * m_cellWidth;
    }

    /// endregion </Border size>

    /// region <Address area width>

    quint64 addressWidth = 0;
    quint64 maxAddr = m_document->baseAddress() + this->rendererLength() - 1;
    if (maxAddr <= 0xFFFF)
    {
        addressWidth = 4;
    }
    else if (maxAddr <= 0xFFFFFFFF)
    {
        addressWidth = 8;
    }
    else
    {
        addressWidth = QString::number(maxAddr, 16).length();
    }

    m_addressWidthSymbols = addressWidth;

    /// endregion </Address area width>

    /// region <Hex line width>

    m_hexLineWidthSymbols = DEFAULT_HEX_LINE_LENGTH;

    if (m_document)
    {
        m_hexLineWidthSymbols = m_document->hexLineWidth();
    }

    /// endregion </Hex line width>

    m_addressColumnWidth = m_cellWidth * m_addressWidthSymbols;
    m_hexColumnWidth = m_cellWidth * (hexLineWidth() * 3);
    n_asciiColumnWidth =  m_cellWidth * hexLineWidth();

    m_addressColumnX = 0;
    m_hexColumnX = m_cellWidth * m_addressWidthSymbols + 2 * m_borderSize;
    m_asciiColumnX = m_hexColumnX + m_cellWidth * (hexLineWidth() * 3) + 2 * m_borderSize;
    m_endColumnX = m_asciiColumnX + m_cellWidth * hexLineWidth() + 2 * m_borderSize;
}

QString QHexRenderer::hexString(quint64 line, QByteArray* rawline) const
{
    QByteArray lrawline = this->getLine(line);

    if (rawline)
        *rawline = lrawline;

    return lrawline.toHex(' ').toUpper() + " ";
}

QString QHexRenderer::asciiString(quint64 line, QByteArray* rawline) const
{
    QByteArray lrawline = this->getLine(line);

    if (rawline)
        *rawline = lrawline;

    QByteArray ascii = lrawline;
    this->unprintableChars(ascii);
    return ascii;
}

QByteArray QHexRenderer::getLine(quint64 line) const
{
    return m_document->read(line * hexLineWidth(), hexLineWidth());
}

void QHexRenderer::blinkCursor()
{
    m_cursorenabled = !m_cursorenabled;
}

quint64 QHexRenderer::rendererLength() const
{
    quint64 result = m_document->dataLength();

    return result;
}

quint64 QHexRenderer::getAddressWidthSymbols() const
{
    return m_addressWidthSymbols;
}

quint64 QHexRenderer::getHexColumnX() const
{
    return m_hexColumnX;
}

quint64 QHexRenderer::getAsciiColumnX() const
{
    return m_asciiColumnX;
}

quint64 QHexRenderer::getEndColumnX() const
{
    return m_endColumnX;
}

quint64 QHexRenderer::getCellWidth() const
{
    return m_cellWidth;
}

/// Get half-byte index based on mouse cursor coordinates
/// \param line
/// \param relx
/// \return
quint64 QHexRenderer::getNibbleIndex(int line, int relx) const
{
    QString hexValue = this->hexString(line);

    for (int i = 0; i < hexValue.size(); i++)
    {
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
        int x = m_fontmetrics.horizontalAdvance(hexValue, i + 1);
#else
        int x = m_fontmetrics.width(hexstring, i + 1);
#endif
        if (x < relx)
            continue;

        if ((i == (hexValue.size() - 1)) || (hexValue[i + 1] == ' '))
            return 0;

        break;
    }

    return 1;
}

void QHexRenderer::unprintableChars(QByteArray &ascii) const
{
    for (char& ch : ascii)
    {
        if (std::isprint(static_cast<int>(ch)))
            continue;

        ch = HEX_UNPRINTABLE_CHAR;
    }
}

void QHexRenderer::renderDocumentStyles(QPainter *painter, QTextDocument* textdocument) const
{
    textdocument->setDocumentMargin(0);
    textdocument->setUndoRedoEnabled(false);
    textdocument->setDefaultFont(painter->font());
}

void QHexRenderer::renderBasicStyle(QTextCursor &textcursor, const QByteArray &rawline, int factor) const
{
    QPalette palette = qApp->palette();
    QColor color = palette.color(QPalette::WindowText);

    if (color.lightness() < 50)
    {
        if (color == Qt::black)
            color = Qt::gray;
        else
            color = color.lighter();
    }
    else
        color = color.darker();

    QTextCharFormat charformat;
    charformat.setForeground(color);

    for (int i = 0; i < rawline.length(); i++)
    {
        if ((rawline[i] != 0x00) && (static_cast<uchar>(rawline[i]) != 0xFF))
            continue;

        textcursor.setPosition(i * factor);
        textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, factor);
        textcursor.setCharFormat(charformat);
    }
}

void QHexRenderer::renderMetadata(QTextCursor &textcursor, quint64 line, int factor) const
{
    QHexMetadata* metadata = m_document->metadata();

    if (!metadata->hasMetadata(line))
        return;

    const QHexLineMetadata& linemetadata = metadata->get(line);

    for (const QHexMetadataItem& mi : linemetadata)
    {
        QTextCharFormat charformat;

        if (mi.background.isValid())
            charformat.setBackground(mi.background);

        if (mi.foreground.isValid())
            charformat.setForeground(mi.foreground);

        if (!mi.comment.isEmpty())
            charformat.setUnderlineStyle(QTextCharFormat::SingleUnderline);

        textcursor.setPosition(mi.start * factor);
        textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, (mi.length * factor) - (factor > 1 ? 1 : 0));
        textcursor.setCharFormat(charformat);
    }
}

void QHexRenderer::renderSelectionAscii(QTextCursor& textcursor, quint64 line) const
{
    QHexCursor* cursor = m_document->cursor();

    if (!cursor->isLineSelected(line))
        return;

    const QHexPosition& startsel = cursor->selectionStart();
    const QHexPosition& endsel = cursor->selectionEnd();
    const quint8 maxLineWidth = m_document->hexLineWidth();
    int startPos;
    int endPos;

    if (startsel.line == endsel.line)
    {
        // If both start and end of selection located on the same line
        startPos = startsel.column;
        endPos = endsel.column + 1;

        textcursor.setPosition(startPos);
        textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, endPos);
    }
    else
    {
        // Multi-line selection
        startPos = startsel.column;
        endPos = endsel.column + 1;

        // For all intermediary lines (non-start, non-end):
        // They should cover all width with selection
        {
            if (line != startsel.line)
                startPos = 0;

            if (line != endsel.line)
                endPos = maxLineWidth;
        }
    }

    // Apply selection to text cursor
    textcursor.setPosition(startPos);
    textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, endPos);

    QPalette palette = qApp->palette();

    QTextCharFormat charformat;
    charformat.setBackground(palette.color(QPalette::Highlight));
    charformat.setForeground(palette.color(QPalette::HighlightedText));
    textcursor.setCharFormat(charformat);
}

void QHexRenderer::renderSelectionHex(QTextCursor& textcursor, quint64 line) const
{
    QHexCursor* cursor = m_document->cursor();

    if (!cursor->isLineSelected(line))
        return;

    const QHexPosition& startsel = cursor->selectionStart();
    const QHexPosition& endsel = cursor->selectionEnd();
    const quint8 maxLineWidth = m_document->hexLineWidth();
    const int factor = 3; // Each hex byte is represented by 2xNumbers + 1xSpace symbols
    int startPos;
    int endPos;

    if (startsel.line == endsel.line)
    {
        // If both start and end of selection located on the same line
        startPos = startsel.column * factor;
        endPos = (endsel.column + 1) * factor - 1;
    }
    else
    {
        // Multi-line selection
        startPos = startsel.column * factor;
        endPos = (endsel.column + 1) * factor - 1;


        // For all intermediary lines (non-start, non-end):
        // They should cover all width with selection
        {
            if (line != startsel.line)
                startPos = 0;

            if (line != endsel.line)
                endPos = maxLineWidth * factor - 1;
        }
    }

    // Apply selection to text cursor
    textcursor.setPosition(startPos);
    textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, endPos);

    QPalette palette = qApp->palette();

    QTextCharFormat charformat;
    charformat.setBackground(palette.color(QPalette::Highlight));
    charformat.setForeground(palette.color(QPalette::HighlightedText));
    textcursor.setCharFormat(charformat);
}

void QHexRenderer::renderCursorAscii(QTextCursor &textcursor, quint64 line) const
{
    QHexCursor* cursor = m_document->cursor();

    if ((line != cursor->currentLine()) || !m_cursorenabled)
        return;

    textcursor.clearSelection();
    textcursor.setPosition(m_document->cursor()->currentColumn());
    textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);

    QPalette palette = qApp->palette();

    QTextCharFormat charformat;

    if ((cursor->insertionMode() == QHexCursor::OverwriteMode) || (m_selectedArea != QHexRenderer::AsciiArea))
    {
        charformat.setForeground(palette.color(QPalette::Window));

        if (m_selectedArea == QHexRenderer::AsciiArea)
            charformat.setBackground(palette.color(QPalette::WindowText));
        else
            charformat.setBackground(palette.color(QPalette::WindowText).lighter(250));
    }
    else
    {
        charformat.setUnderlineStyle(QTextCharFormat::UnderlineStyle::SingleUnderline);
    }

    textcursor.setCharFormat(charformat);
}

void QHexRenderer::renderCursorHex(QTextCursor &textcursor, quint64 line) const
{
    QHexCursor* cursor = m_document->cursor();

    if ((line != cursor->currentLine()) || !m_cursorenabled)
        return;

    textcursor.clearSelection();
    textcursor.setPosition(m_document->cursor()->currentColumn() * 3);

    if ((m_selectedArea == QHexRenderer::HexArea) && !m_document->cursor()->currentNibble())
        textcursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor);

    textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);

    if (m_selectedArea == QHexRenderer::AsciiArea)
        textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);

    QPalette palette = qApp->palette();
    QTextCharFormat charformat;

    if ((cursor->insertionMode() == QHexCursor::OverwriteMode) || (m_selectedArea != QHexRenderer::HexArea))
    {
        charformat.setForeground(palette.color(QPalette::Window));

        if (m_selectedArea == QHexRenderer::HexArea)
            charformat.setBackground(palette.color(QPalette::WindowText));
        else
            charformat.setBackground(palette.color(QPalette::WindowText).lighter(250));
    }
    else
        charformat.setUnderlineStyle(QTextCharFormat::UnderlineStyle::SingleUnderline);

    textcursor.setCharFormat(charformat);
}

void QHexRenderer::drawAddress(QPainter* painter, const QPalette& palette, const QRect& linerect, quint64 line)
{
    quint64 addr = line * hexLineWidth() + m_document->baseAddress();
    QString addrStr = QString::number(addr, 16).rightJustified(this->getAddressWidthSymbols(), QLatin1Char('0')).toUpper();

    QRect addressrect = linerect;
    addressrect.setWidth(this->getHexColumnX());

    painter->save();
    painter->setPen(palette.color(QPalette::Highlight));
    painter->drawText(addressrect, Qt::AlignHCenter | Qt::AlignVCenter, addrStr);
    painter->restore();
}

void QHexRenderer::drawHex(QPainter *painter, const QPalette &palette, const QRect &linerect, quint64 line)
{
    Q_UNUSED(palette)
    QTextDocument textdocument;
    QTextCursor textcursor(&textdocument);
    QByteArray rawline;

    textcursor.insertText(this->hexString(line, &rawline));

    if (line == this->documentLastLine())
        textcursor.insertText(" ");

    QRect hexrect = linerect;
    hexrect.setX(this->getHexColumnX() + this->borderSize());

    this->renderDocumentStyles(painter, &textdocument);
    this->renderBasicStyle(textcursor, rawline, 3);
    this->renderMetadata(textcursor, line, 3);
    this->renderSelectionHex(textcursor, line);
    this->renderCursorHex(textcursor, line);

    painter->save();
    painter->translate(hexrect.topLeft());
    textdocument.drawContents(painter);
    painter->restore();
}

void QHexRenderer::drawAscii(QPainter *painter, const QPalette &palette, const QRect &linerect, quint64 line)
{
    Q_UNUSED(palette)
    QTextDocument textdocument;
    QTextCursor textcursor(&textdocument);
    QByteArray rawline;
    textcursor.insertText(this->asciiString(line, &rawline));

    if (line == this->documentLastLine())
        textcursor.insertText(" ");

    QRect asciirect = linerect;
    asciirect.setX(this->getAsciiColumnX() + this->borderSize());

    this->renderDocumentStyles(painter, &textdocument);
    this->renderBasicStyle(textcursor, rawline);
    this->renderMetadata(textcursor, line);
    this->renderSelectionAscii(textcursor, line);
    this->renderCursorAscii(textcursor, line);

    painter->save();
    painter->translate(asciirect.topLeft());
    textdocument.drawContents(painter);
    painter->restore();
}

/// Draw hex viewer header (Offset | 00 ... 07 | ASCII)
/// \param painter
/// \param palette
void QHexRenderer::drawHeader(QPainter *painter, const QPalette &palette)
{
    QRect rect = QRect(0, 0, this->getEndColumnX(), headerLineCount() * lineHeight());
    QString hexHeader;
    for (quint8 i = 0; i < hexLineWidth(); i++)
    {
        hexHeader.append(QString("%1 ").arg(QString::number(i, 16).rightJustified(2, QChar('0'))).toUpper());
    }

    QRect addressRect = rect;
    addressRect.setWidth(this->getHexColumnX());

    QRect hexRect = rect;
    hexRect.setX(this->getHexColumnX() + this->borderSize());
    hexRect.setWidth(this->getCellWidth() * (hexLineWidth() * 3));

    QRect asciiRect = rect;
    asciiRect.setX(this->getAsciiColumnX());
    asciiRect.setWidth(this->getEndColumnX() - this->getAsciiColumnX());

    painter->save();
    painter->setPen(palette.color(QPalette::Highlight));

    painter->drawText(addressRect, Qt::AlignHCenter | Qt::AlignVCenter, QString("Offset"));

    // Align left for maximum consistency with drawHex() which prints from the left.
    // so hex and positions are aligned vertically
    painter->drawText(hexRect, Qt::AlignLeft | Qt::AlignVCenter, hexHeader);
    painter->drawText(asciiRect, Qt::AlignHCenter | Qt::AlignVCenter, QString("Ascii"));

    painter->restore();
}
