#pragma once

/// @file journalspanslider.h
/// @brief The TTD timeline scrubber with the write journal's spans drawn on it (D40).

#include <QSlider>
#include <utility>
#include <vector>

/// A QSlider that draws, along its bottom edge, the spans of the session the
/// write journal covers: there "who wrote this address last" answers at once
class JournalSpanSlider : public QSlider
{
public:
    using QSlider::QSlider;
    /// Each span as fractions of the slider's range, 0..1
    void setJournalSpans(std::vector<std::pair<double, double>> spans);
    const std::vector<std::pair<double, double>>& journalSpans() const { return _spans; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<std::pair<double, double>> _spans;
};
