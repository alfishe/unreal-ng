#pragma once

#ifndef LOGVIEWER_H
#define LOGVIEWER_H

#include <QPlainTextEdit>
#include <QWidget>

class QButtonGroup;
class QTextCodec;
class QStandardItemModel;
class LineNumberArea;

#define LINENUMBER_AREA_LEFT_MARGIN 2
#define LINENUMBER_AREA_RIGHT_MARGIN 5

class LogViewer : public QPlainTextEdit
{
    Q_OBJECT

public:
    LogViewer(QWidget* parent = nullptr, bool showLineNumber = true);
    virtual ~LogViewer();

protected:
    void init();

    // Line numbering area
public:
    int lineNumberAreaWidth();
    void lineNumberAreaPaintEvent(QPaintEvent* event);

    void setTabWidth(int spaceCount);

    // QWidget event handlers
protected:
    virtual void resizeEvent(QResizeEvent* event);
    virtual void wheelEvent(QWheelEvent* event);
    virtual void paintEvent(QPaintEvent* event);

    // Fields
protected:
    bool m_showLineNumber;
    bool m_isFirstAppend;
    bool m_isZoomMode;
    long m_fileSize;

    QColor m_customBackgroundColor;
    QColor m_currentLineFgColor;
    QColor m_currentLineBgColor;

    LineNumberArea* m_lineNumberArea;

    QTextCodec* m_textCodec;
};

class LineNumberArea : public QWidget
{
public:
    LineNumberArea(LogViewer* viewer) : QWidget(viewer)
    {
        m_viewer = viewer;
    }

    QSize sizeHint() const
    {
        return QSize(m_viewer->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent *event)
    {
        m_viewer->lineNumberAreaPaintEvent(event);
    }

private:
    LogViewer* m_viewer;
};


#endif // LOGVIEWER_H
