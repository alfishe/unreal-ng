/**
 * @file midiactivitywindow.h
 * @brief MidiActivityWindow - Tools > MIDI Activity: the ZX-MultiSound's SAM2695 synthesizer as it plays: per part the
 *        program and its preset name, volume, pan and voices, a keyboard strip with the notes sounding, the line and
 *        the counters, and a Panic button (ZX-MultiSound tdd-integration.md §5).
 *
 * Reads DeviceState::Midi (the report every automation surface returns) ten times a second while visible; the panic
 * is MidiControl (a TTD live input), as on every surface.
 */

#pragma once

#include <array>
#include <cstdint>

#include <QWidget>

#include "emulator/state/statenode.h"

class EmulatorBinding;
class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;

/// 16 rows of 128 keys: a key lit while a voice of that part sounds it
class MidiKeyStrip : public QWidget
{
    Q_OBJECT

public:
    explicit MidiKeyStrip(QWidget* parent = nullptr);

    /// The keys sounding per part (bit k of keys[part][k / 64])
    void setKeys(const std::array<std::array<uint64_t, 2>, 16>& keys);
    const std::array<std::array<uint64_t, 2>, 16>& keys() const
    {
        return _keys;
    }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::array<std::array<uint64_t, 2>, 16> _keys{};
};

class MidiActivityWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MidiActivityWindow(QWidget* parent = nullptr);

    void setBinding(EmulatorBinding* binding);

    /// Reads the report now (the timer calls it; tests too)
    void refresh();
    /// A part's row text in the table (tests): column 0-6
    QString cellText(int part, int column) const;
    const MidiKeyStrip* keyStrip() const
    {
        return _strip;
    }

signals:
    void visibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void panic();

    EmulatorBinding* _binding = nullptr;
    QTimer* _timer = nullptr;
    QLabel* _summary = nullptr;
    QTableWidget* _parts = nullptr;
    MidiKeyStrip* _strip = nullptr;
    QPushButton* _panic = nullptr;
    QLabel* _panicResult = nullptr;
};
