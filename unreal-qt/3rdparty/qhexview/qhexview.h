#ifndef QHEXVIEW_H
#define QHEXVIEW_H

#define QHEXVIEW_VERSION 3.0

#include <QAbstractScrollArea>
#include <QTimer>
#include "document/qhexrenderer.h"
#include "document/qhexdocument.h"

class QHexView : public QAbstractScrollArea
{
    Q_OBJECT

    public:
        explicit QHexView(QWidget *parent = nullptr);
        QHexDocument* document();
        void setDocument(QHexDocument* document);
        void setReadOnly(bool b);

    protected:
        virtual bool event(QEvent* e) override;
        virtual void scrollContentsBy(int dx, int dy) override;
        virtual void keyPressEvent(QKeyEvent *e) override;
        virtual void mousePressEvent(QMouseEvent* e) override;
        virtual void mouseMoveEvent(QMouseEvent* e) override;
        virtual void mouseReleaseEvent(QMouseEvent* e) override;
        virtual void focusInEvent(QFocusEvent* e) override;
        virtual void focusOutEvent(QFocusEvent* e) override;
        virtual void wheelEvent(QWheelEvent* e) override;
        virtual void resizeEvent(QResizeEvent* e) override;
        virtual void paintEvent(QPaintEvent* e) override;

    private slots:
        void renderCurrentLine();
        void moveToSelection();
        void blinkCursor();

    private:
        void moveNext(bool select = false);
        void movePrevious(bool select = false);

    private:
        bool processMove(QHexCursor* cur, QKeyEvent* e);
        bool processTextInput(QHexCursor* cur, QKeyEvent* e);
        bool processAction(QHexCursor* cur, QKeyEvent* e);
        void adjustScrollBars();
        void renderLine(quint64 line);
        quint64 firstVisibleLine() const;
        quint64 lastVisibleLine() const;
        quint64 visibleLines() const;
        bool isLineVisible(quint64 line) const;

        int documentSizeFactor() const;
        QPoint absolutePosition(const QPoint & pos) const;

    private:
        QHexDocument* m_document;
        QHexRenderer* m_renderer;
        QTimer* m_blinktimer;
        bool m_readonly;

        QHexRenderer::AreaTypeEnum m_selectionArea;
};

#endif // QHEXVIEW_H
