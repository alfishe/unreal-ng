#pragma once

/// @file journalspanslider.h
/// @brief The TTD timeline scrubber with the write journal's spans drawn on it (D40), and with branches the parts
/// of the active lineage each branch owns and the marks where other branches leave it (TTD branches, surfaces.md §1).

#include <cstdint>

#include <QColor>
#include <QSlider>
#include <QString>
#include <functional>
#include <utility>
#include <vector>

/// A QSlider that draws, along its bottom edge, the spans of the session the
/// write journal covers: there "who wrote this address last" answers at once.
/// Along its top edge: the lineage's parts by branch (a tint each) and the fork
/// marks, with the branches leaving there as the mark's tooltip
class JournalSpanSlider : public QSlider
{
public:
    using QSlider::QSlider;

    /// A part of the active lineage owned by one branch, as fractions of the range
    struct BranchSpan
    {
        double from = 0.0;
        double to = 0.0;
        QColor color;
        bool operator==(const BranchSpan& o) const { return from == o.from && to == o.to && color == o.color; }
    };
    /// Where other branches leave the lineage: a fraction of the range, how many leave there, what they are
    struct ForkMark
    {
        double at = 0.0;
        int count = 1;
        QString tip;
        std::vector<uint32_t> branches;
        bool operator==(const ForkMark& o) const
        {
            return at == o.at && count == o.count && tip == o.tip && branches == o.branches;
        }
    };

    /// Each span as fractions of the slider's range, 0..1
    void setJournalSpans(std::vector<std::pair<double, double>> spans);
    const std::vector<std::pair<double, double>>& journalSpans() const { return _spans; }
    void setBranchSpans(std::vector<BranchSpan> spans);
    const std::vector<BranchSpan>& branchSpans() const { return _branchSpans; }
    void setForkMarks(std::vector<ForkMark> marks);
    const std::vector<ForkMark>& forkMarks() const { return _marks; }
    /// The mark under @p x (widget pixels), or -1
    int markAt(int x) const;
    /// A double click on a mark (its index in forkMarks())
    std::function<void(int)> onMarkActivated;

protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QRect Groove() const;

    std::vector<std::pair<double, double>> _spans;
    std::vector<BranchSpan> _branchSpans;
    std::vector<ForkMark> _marks;
};
