#pragma once

#define QHEXVIEW_VERSION 5.0

#include <QAbstractScrollArea>
#include <QFontMetricsF>
#include <QHexView/model/qhexcursor.h>
#include <QHexView/model/qhexdelegate.h>
#include <QHexView/model/qhexdocument.h>
#include <QList>
#include <QRectF>
#include <QRegion>
#include <QTextCharFormat>
#include <QHash>
#include <QPixmap>
#include <QTextLayout>
#include <limits>
#include <functional>
#include <memory>
#include <vector>

#if defined(QHEXVIEW_ENABLE_DIALOGS)
class HexFindDialog;
#endif

class QHexView: public QAbstractScrollArea {
    Q_OBJECT

public:
    enum class CopyMode { Visual, HexArraySquare, HexArrayCurly, HexArrayChar };
    Q_ENUM(CopyMode);

public:
    explicit QHexView(QWidget* parent = nullptr);
    QRectF headerRect() const;
    QRectF addressRect() const;
    QRectF hexRect() const;
    QRectF asciiRect() const;
    QHexDocument* hexDocument() const;
    QHexCursor* hexCursor() const;
    const QHexMetadata* hexMetadata() const;
    QHexOptions options() const;
    QColor getReadableColor(QColor c) const;
    QByteArray selectedBytes() const;
    QByteArray getLine(qint64 line) const;
    unsigned int addressWidth() const;
    unsigned int lineLength() const;
    bool isModified() const;
    bool canUndo() const;
    bool canRedo() const;
    quint64 offset() const;
    quint64 address() const;
    QHexPosition positionFromOffset(quint64 offset) const;
    QHexPosition positionFromAddress(quint64 address) const;
    QHexPosition position() const;
    QHexPosition selectionStart() const;
    QHexPosition selectionEnd() const;
    quint64 selectionStartOffset() const;
    quint64 selectionEndOffset() const;
    quint64 baseAddress() const;
    quint64 lines() const;
    qint64 replace(const QVariant& oldvalue, const QVariant& newvalue,
                   qint64 offset, QHexFindMode mode = QHexFindMode::Text,
                   unsigned int options = QHexFindOptions::None,
                   QHexFindDirection fd = QHexFindDirection::Forward) const;
    qint64 find(const QVariant& value, qint64 offset,
                QHexFindMode mode = QHexFindMode::Text,
                unsigned int options = QHexFindOptions::None,
                QHexFindDirection fd = QHexFindDirection::Forward) const;
    QHexDocument* getDocument() const { return m_hexdocument; };
    QHexMetadata* getMetadata() const { return m_hexmetadata; };
    void gotoOffset(quint64 offset) const;
    void setOptions(const QHexOptions& options);
    void setBaseAddress(quint64 baseaddress);
    void setDelegate(QHexDelegate* rd);
    void setDocument(QHexDocument* doc);
    void setData(const QByteArray& ba);
    void setData(QHexBuffer* buffer);
    void setCursorMode(QHexCursor::Mode mode);
    void setByteColor(quint8 b, QHexColor c);
    void setByteForeground(quint8 b, QColor c);
    void setByteBackground(quint8 b, QColor c);
    void setMetadata(qint64 begin, qint64 end, const QColor& fgcolor,
                     const QColor& bgcolor, const QString& comment);
    void setForeground(qint64 begin, qint64 end, const QColor& fgcolor);
    void setBackground(qint64 begin, qint64 end, const QColor& bgcolor);
    void setComment(qint64 begin, qint64 end, const QString& comment);
    void setMetadataSize(qint64 begin, qint64 length, const QColor& fgcolor,
                         const QColor& bgcolor, const QString& comment);
    void setForegroundSize(qint64 begin, qint64 length, const QColor& fgcolor);
    void setBackgroundSize(qint64 begin, qint64 length, const QColor& bgcolor);
    void setCommentSize(qint64 begin, qint64 length, const QString& comment);
    void removeMetadata(qint64 line);
    void removeBackground(qint64 line);
    void removeForeground(qint64 line);
    void removeComments(qint64 line);
    void unhighlight(qint64 line);
    void clearMetadata();
    void reset();

public Q_SLOTS:
#if defined(QHEXVIEW_ENABLE_DIALOGS)
    void showFind();
    void showReplace();
#endif
    void undo();
    void redo();
    void cut(bool hex = false);
    void copyAs(CopyMode mode = CopyMode::Visual) const;
    void copy(bool hex = false) const;
    void paste(bool hex = false);
    void clearModified();
    void selectAll();
    void removeSelection();
    void switchMode();
    void setAddressWidth(unsigned int w);
    void setLineLength(unsigned int l);
    void setGroupLength(unsigned int l);
    void setScrollSteps(int scrollsteps);
    void setReadOnly(bool r);
    bool isReadOnly() const { return m_readonly; }  // unreal-ng: the remote-control lock restores it
    void setAutoWidth(bool r);
    // unreal-ng: the tooltip of the byte under the mouse comes from the host (empty: the metadata comments)
    void setToolTipProvider(std::function<QString(qint64 offset)> provider) { m_tooltipprovider = std::move(provider); }
    // unreal-ng: the changes made between beginChanges() and endChanges() (a new document, the metadata, the cursor)
    // repaint only the rows whose painted content they change, not the whole view (a debugger stop moves the PC
    // highlight by a row or two); a change of the view itself (a scroll, the size, the options) repaints it all
    void beginChanges();
    void endChanges();
    // unreal-ng: rows painted as kept QTextLayouts (default) or through a QTextDocument per paint, as upstream
    qsizetype rowPixmapCount() const { return m_rowPixmaps.size(); }  // the rows kept as pixels (tests)
    // unreal-ng: whether text drawn into a pixmap gets the glyphs it gets on the widget. Not with FreeType (Linux, the
    // BSDs, the Steam Deck, Windows' offscreen platform or fontengine=freetype): QRasterPaintEngine draws a widget's
    // glyphs in the desktop's antialiasing (subpixel / LCD when it is on) and a pixmap's in gray (Format_A8), so the
    // rows are drawn from their kept layouts straight onto the widget there. CoreText (Apple) and Windows' own engines
    // pick the glyphs by the target image (ClearType on an opaque RGB32 pixmap as on the backing store): the rows are
    // kept as pixmaps
    static bool rowPixmapsDrawLikeTheWidget();
    void setRowLayouts(bool on) {
        m_rowLayoutsOn = on;
        m_rowLayouts.clear();
        m_rowPixmaps.clear();
        this->viewport()->update();
    }

private:
    void paint(QPainter* painter, const QRegion& exposed) const;
    void updateView();                    // unreal-ng: a full repaint, or one deferred to endChanges()
    QByteArray paintKey() const;          // unreal-ng: what every painted row depends on
    QByteArray headerKey() const;         // unreal-ng: what the header row depends on
    QByteArray lineKey(qint64 line) const;  // unreal-ng: what the row of a document line depends on
    void checkOptions();
    void checkState();
    void checkAndUpdate(bool calccolumns = false);
    void calcColumns();
    void ensureVisible();
    void drawSeparators(QPainter* p) const;
    // unreal-ng: `Sink` is a QTextCursor (the document path) or the row texts of paintRows (QHexRowSink)
    template<typename Sink> void drawHeader(Sink& c) const;
    template<typename Sink>
    void drawDocument(Sink& c, qint64 first = 0,
                      qint64 last = std::numeric_limits<qint64>::max()) const;
    template<typename Sink>
    QTextCharFormat drawFormat(Sink& c, quint8 b, const QString& s,
                               QHexArea area, qint64 line, qint64 column,
                               bool applyformat) const;
    // unreal-ng: paints the view rows of `runs` ([first, last], the header is row 0) as one QTextLayout per row, kept
    // for the next paint while the row's key stays; false (nothing painted) when a row needs the document path (a
    // block background: the styled header, line backgrounds)
    bool paintRows(QPainter* painter, const std::vector<std::pair<qint64, qint64>>& runs, qint64 rows,
                   int headerrows, const QByteArray& key) const;
    unsigned int calcAddressWidth() const;
    int visibleLines(bool absolute = false) const;
    qint64 getLastColumn(qint64 line) const;
    qint64 lastLine() const;
    qreal getNCellsWidth(int n) const;
    qreal hexColumnWidth() const;
    qreal hexColumnX() const;
    qreal asciiColumnX() const;
    qreal endColumnX() const;
    qreal cellWidth() const;
    qreal lineHeight() const;
    qint64 positionFromLineCol(qint64 line, qint64 col) const;
    QHexPosition positionFromPoint(QPoint pt) const;
    QPoint absolutePoint(QPoint pt) const;
    QHexArea areaFromPoint(QPoint pt) const;
    void moveNext(bool select = false);
    void movePrevious(bool select = false);
    bool keyPressMove(QKeyEvent* e);
    bool keyPressTextInput(QKeyEvent* e);
    bool keyPressAction(QKeyEvent* e);

protected:
    bool event(QEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void paintEvent(QPaintEvent*) override;
    void scrollContentsBy(int dx, int dy) override;  // unreal-ng: a repaint deferred in a batch of changes
    void resizeEvent(QResizeEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    static QString reduced(const QString& s, int maxlen);
    static bool isColorLight(QColor c);

Q_SIGNALS:
    void dataChanged(const QByteArray& data, quint64 offset,
                     QHexDocument::ChangeReason reason);
    void modifiedChanged(bool modified);
    void positionChanged();
    void modeChanged();

private:
    bool m_readonly{false}, m_writing{false}, m_autowidth{false};
    std::function<QString(qint64)> m_tooltipprovider;
    QHexArea m_currentarea{QHexArea::Ascii};
    QList<QRectF> m_hexcolumns;
    QFontMetricsF m_fontmetrics;
    QHexOptions m_options;
    QHexCursor* m_hexcursor{nullptr};
    QHexDocument* m_hexdocument{nullptr};
    QHexMetadata* m_hexmetadata{nullptr};
    QHexDelegate* m_hexdelegate{nullptr};
    quint8 m_hexLineWidth;
    // unreal-ng: the batched changes (beginChanges) and what the rows on screen were painted from
    int m_changes{0};
    bool m_changesPending{false};
    bool m_ensureVisiblePending{false};
    mutable qreal m_blockHeight{0};
    mutable QByteArray m_paintedKey, m_paintedHeader;
    mutable quint64 m_paintedTop{0};  // the first document line painted
    mutable std::vector<QByteArray> m_paintedLines;  // per visible row, from the top one
    // unreal-ng: the rows laid out (paintRows) by their key, for the view's paintKey m_rowLayoutsKey
    mutable QHash<QByteArray, std::shared_ptr<QTextLayout>> m_rowLayouts;
    mutable QByteArray m_rowLayoutsKey;
    mutable QHash<QByteArray, QPixmap> m_rowPixmaps;  // unreal-ng: those rows' pixels, by their key
    bool m_rowLayoutsOn{true};
#if defined(QHEXVIEW_ENABLE_DIALOGS)
    HexFindDialog *m_hexdlgfind{nullptr}, *m_hexdlgreplace{nullptr};
#endif

    friend class QHexDelegate;
    friend class QHexCursor;
};
