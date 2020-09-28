#include "logviewer.h"

#include <QTextBlock>
#include <QApplication>
#include <QWheelEvent>
#include <QPainter>
#include <QScrollBar>
#include <QButtonGroup>
#include <QAbstractButton>
#include <QtCore/QRegExp>
#include <QtCore/QUrl>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QTextCodec>

LogViewer::LogViewer(QWidget* parent, bool showLineNumber) : QPlainTextEdit(parent)
{
    m_showLineNumber = showLineNumber;
    m_isZoomMode = false;

    m_textCodec = QTextCodec::codecForLocale();
    m_lineNumberArea = new LineNumberArea(this);
}

LogViewer::~LogViewer()
{
    if (m_lineNumberArea)
    {
        delete m_lineNumberArea;
        m_lineNumberArea = nullptr;
    }
}

void LogViewer::init()
{
    setReadOnly(true);
    setTabWidth(2);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);

    QPalette palette;
    palette.setColor(QPalette::Inactive, QPalette::Highlight, palette.color(QPalette::Active, QPalette::Highlight));
    palette.setColor(QPalette::Inactive, QPalette::HighlightedText, palette.color(QPalette::Active, QPalette::HighlightedText));
    setPalette(palette);
}

int LogViewer::lineNumberAreaWidth()
{
    int digits = 1;
    int max = qMax(1, document()->blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }

    const int leftMargin = LINENUMBER_AREA_LEFT_MARGIN;
    int space = 3 + fontMetrics().width(QLatin1Char('9')) * (digits + leftMargin);

    return space;
}

void LogViewer::lineNumberAreaPaintEvent(QPaintEvent* event)
{
    if (!m_showLineNumber)
        return ;

    QPainter painter(m_lineNumberArea);
    QColor bgColor = QColor(Qt::lightGray).lighter(125);
    painter.fillRect(event->rect(), bgColor);

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + static_cast<int>(blockBoundingGeometry(block).height());

    while (block.isValid() && top <= event->rect().bottom())
    {
        if (block.isVisible() && bottom >= event->rect().top())
        {
            QString number = QString::number(blockNumber + 1);

            textCursor().blockNumber() == blockNumber ? painter.setPen(Qt::black) : painter.setPen(Qt::gray);
            painter.drawText(0, top, m_lineNumberArea->width() - LINENUMBER_AREA_RIGHT_MARGIN, fontMetrics().height(), Qt::AlignRight, number);
        }

        block = block.next();
        top = bottom;
        bottom = top + static_cast<int>(blockBoundingRect(block).height());
        ++blockNumber;
    }

    QPoint p1 = event->rect().topRight();
    QPoint p2 = event->rect().bottomRight();
    painter.setPen(QColor(Qt::darkGreen));
    painter.drawLine(p1, p2);
}

void LogViewer::setTabWidth(int spaceCount)
{
    setTabStopDistance(fontMetrics().averageCharWidth() * spaceCount);
}

// QWidget events
void LogViewer::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);

    if (m_showLineNumber)
    {
        QRect contentRect = contentsRect();
        m_lineNumberArea->setGeometry(QRect(contentRect.left(), contentRect.top(), lineNumberAreaWidth(), contentRect.height()));
    }
}

void LogViewer::wheelEvent(QWheelEvent* event)
{
    int degrees = event->delta() / 8;
    int steps = degrees / 15;

    if (event->modifiers() == Qt::ControlModifier)
    {
        //setZoom(steps);
    }
    else
    {
        QPlainTextEdit::wheelEvent(event);
    }
}


void LogViewer::paintEvent(QPaintEvent* e)
{
    QPainter painter(viewport());
    Q_ASSERT(qobject_cast<QPlainTextDocumentLayout*>(document()->documentLayout()));

    QPointF offset(contentOffset());

    QRect er = e->rect();
    QRect viewportRect = viewport()->rect();

    bool editable = !isReadOnly();

    QTextBlock block = firstVisibleBlock();
    qreal maximumWidth = document()->documentLayout()->documentSize().width();

    // Set a brush origin so that the WaveUnderline knows where the wave started
    painter.setBrushOrigin(offset);

    // keep right margin clean from full-width selection
    int maxX = offset.x() + qMax((qreal)viewportRect.width(), maximumWidth)
               - document()->documentMargin();
    er.setRight(qMin(er.right(), maxX));
    painter.setClipRect(er);

    QAbstractTextDocumentLayout::PaintContext context = getPaintContext();

    while (block.isValid())
    {
        QRectF r = blockBoundingRect(block).translated(offset);
        QTextLayout *layout = block.layout();

        if (!block.isVisible())
        {
            offset.ry() += r.height();
            block = block.next();
            continue;
        }

        if (r.bottom() >= er.top() && r.top() <= er.bottom())
        {

            QTextBlockFormat blockFormat = block.blockFormat();

            QBrush bg = blockFormat.background();
            if (bg != Qt::NoBrush)
            {
                QRectF contentsRect = r;
                contentsRect.setWidth(qMax(r.width(), maximumWidth));
                //fillBackground(&painter, contentsRect, bg);
            }

            QVector<QTextLayout::FormatRange> selections;
            int blpos = block.position();
            int bllen = block.length();
            for (int i = 0; i < context.selections.size(); ++i) {
                const QAbstractTextDocumentLayout::Selection &range = context.selections.at(i);
                const int selStart = range.cursor.selectionStart() - blpos;
                const int selEnd = range.cursor.selectionEnd() - blpos;
                if (selStart < bllen && selEnd > 0
                    && selEnd > selStart) {
                    QTextLayout::FormatRange o;
                    o.start = selStart;
                    o.length = selEnd - selStart;
                    o.format = range.format;
                    selections.append(o);
                } else if (!range.cursor.hasSelection() && range.format.hasProperty(QTextFormat::FullWidthSelection)
                           && block.contains(range.cursor.position())) {
                    // for full width selections we don't require an actual selection, just
                    // a position to specify the line. that's more convenience in usage.
                    QTextLayout::FormatRange o;
                    QTextLine l = layout->lineForTextPosition(range.cursor.position() - blpos);
                    o.start = l.textStart();
                    o.length = l.textLength();
                    if (o.start + o.length == bllen - 1)
                        ++o.length; // include newline
                    o.format = range.format;
                    selections.append(o);
                }
            }

            bool drawCursor = (editable
                               && context.cursorPosition >= blpos
                               && context.cursorPosition < blpos + bllen);

            bool drawCursorAsBlock = drawCursor && overwriteMode();

            if (drawCursorAsBlock)
            {
                if (context.cursorPosition == blpos + bllen - 1)
                {
                    drawCursorAsBlock = false;
                }
                else
                    {
                    QTextLayout::FormatRange o;
                    o.start = context.cursorPosition - blpos;
                    o.length = 1;
                    o.format.setForeground(palette().base());
                    o.format.setBackground(palette().text());
                    selections.append(o);
                }
            }


            layout->draw(&painter, offset, selections, er);
            if ((drawCursor && !drawCursorAsBlock)
                || (editable && context.cursorPosition < -1
                    && !layout->preeditAreaText().isEmpty()))
            {
                int cpos = context.cursorPosition;

                if (cpos < -1)
                    cpos = layout->preeditAreaPosition() - (cpos + 2);
                else
                    cpos -= blpos;

                layout->drawCursor(&painter, offset, cpos, cursorWidth());
            }
        }

        offset.ry() += r.height();
        if (offset.y() > viewportRect.height())
            break;
        block = block.next();
    }

    if (backgroundVisible() && !block.isValid() && offset.y() <= er.bottom()
        && (centerOnScroll() || verticalScrollBar()->maximum() == verticalScrollBar()->minimum()))
    {
        painter.fillRect(QRect(QPoint((int)er.left(), (int)offset.y()), er.bottomRight()), palette().background());
    }
}
