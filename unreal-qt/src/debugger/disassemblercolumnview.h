#ifndef DISASSEMBLERCOLUMNVIEW_H
#define DISASSEMBLERCOLUMNVIEW_H

#include <QSet>
#include <QWidget>

class Z80Disassembler;

class DisassemblerColumnView : public QWidget
{
    Q_OBJECT

private:
    struct ArrowPath
    {
        uint64_t startidx;
        uint64_t endidx;
        QColor color;
    };

public:
    explicit DisassemblerColumnView(QWidget *parent = nullptr);
    void setDisassembler(Z80Disassembler* disassembler);
    void renderArrows(size_t start, size_t count);

protected:
    virtual void paintEvent(QPaintEvent*);

private:
    bool isPathSelected(const ArrowPath& path) const;
    void fillArrow(QPainter* painter, int y, const QFontMetrics &fm);
    //void insertPath(REDasm::ListingItem* fromitem, uint64_t fromidx, uint64_t toidx);

private:
    Z80Disassembler* m_disassembler;
    QList<ArrowPath> m_paths;
    QSet< QPair<uint64_t, uint64_t> > m_done;
    int64_t m_first;
    int64_t m_last;

signals:

};

#endif // DISASSEMBLERCOLUMNVIEW_H
