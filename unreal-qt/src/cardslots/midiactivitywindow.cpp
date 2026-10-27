/**
 * @file midiactivitywindow.cpp
 * @brief The MIDI activity window: DeviceState::Midi, MidiControl panic.
 */

#include "midiactivitywindow.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/midi/midicontrol.h"
#include "emulator/state/devicestate.h"

namespace
{
    QString Q(const std::string& text)
    {
        return QString::fromStdString(text);
    }

    long long Int(const StateNode* node)
    {
        return node != nullptr ? static_cast<long long>(node->kind == StateNode::Kind::Bool ? node->b : node->i) : 0;
    }

    std::string Text(const StateNode* node)
    {
        return node != nullptr && node->kind == StateNode::Kind::String ? node->s : std::string();
    }

    enum Column
    {
        ColChannel,
        ColProgram,
        ColPreset,
        ColVolume,
        ColPan,
        ColVoices,
        ColNotes,
        ColCount
    };
}  // namespace

/// region <MidiKeyStrip>

MidiKeyStrip::MidiKeyStrip(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(16 * 6);
    setToolTip(tr("The notes sounding: one row per MIDI channel 1-16, one column per key 0-127 (C-1 .. G9); "
                  "the darker columns are the black keys"));
}

void MidiKeyStrip::setKeys(const std::array<std::array<uint64_t, 2>, 16>& keys)
{
    if (keys == _keys)
        return;
    _keys = keys;
    update();
}

QSize MidiKeyStrip::sizeHint() const
{
    return {128 * 5, 16 * 8};
}

void MidiKeyStrip::paintEvent(QPaintEvent* event)
{
    (void)event;
    QPainter painter(this);
    const double keyWidth = width() / 128.0;
    const double rowHeight = height() / 16.0;
    static const bool kBlack[12] = {false, true, false, true, false, false, true, false, true, false, true, false};
    const QColor white = palette().color(QPalette::Base);
    const QColor black = palette().color(QPalette::AlternateBase).darker(115);
    const QColor lit = palette().color(QPalette::Highlight);
    for (int part = 0; part < 16; part++)
    {
        for (int key = 0; key < 128; key++)
        {
            const QRectF cell(key * keyWidth, part * rowHeight, keyWidth, rowHeight);
            const bool on = (_keys[size_t(part)][size_t(key >> 6)] >> (key & 63)) & 1;
            painter.fillRect(cell, on ? lit : (kBlack[key % 12] ? black : white));
        }
    }
    painter.setPen(palette().color(QPalette::Mid));
    for (int part = 1; part < 16; part++)
        painter.drawLine(QPointF(0, part * rowHeight), QPointF(width(), part * rowHeight));
}

/// endregion </MidiKeyStrip>

MidiActivityWindow::MidiActivityWindow(QWidget* parent) : QWidget(parent)
{
    setWindowTitle(tr("MIDI Activity"));
    auto* layout = new QVBoxLayout(this);
    auto* top = new QHBoxLayout();
    _summary = new QLabel(this);
    _summary->setWordWrap(true);
    top->addWidget(_summary, 1);
    _panic = new QPushButton(tr("Panic"), this);
    _panic->setToolTip(tr("Every voice of the synthesizer stops; programs, controllers and the MIDI stream in progress stay"));
    top->addWidget(_panic);
    layout->addLayout(top);
    _panicResult = new QLabel(this);
    layout->addWidget(_panicResult);

    _parts = new QTableWidget(16, ColCount, this);
    _parts->setHorizontalHeaderLabels({tr("Ch"), tr("Program"), tr("Preset"), tr("Volume"), tr("Pan"), tr("Voices"), tr("Notes")});
    _parts->verticalHeader()->setVisible(false);
    _parts->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _parts->horizontalHeader()->setStretchLastSection(true);
    for (int part = 0; part < 16; part++)
        for (int column = 0; column < ColCount; column++)
            _parts->setItem(part, column, new QTableWidgetItem());
    layout->addWidget(_parts, 2);

    _strip = new MidiKeyStrip(this);
    layout->addWidget(_strip, 1);

    connect(_panic, &QPushButton::clicked, this, &MidiActivityWindow::panic);
    _timer = new QTimer(this);
    _timer->setInterval(100);
    connect(_timer, &QTimer::timeout, this, &MidiActivityWindow::refresh);
    resize(720, 640);
}

void MidiActivityWindow::setBinding(EmulatorBinding* binding)
{
    _binding = binding;
}

void MidiActivityWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refresh();
    _timer->start();
    emit visibilityChanged(true);
}

void MidiActivityWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    _timer->stop();
    emit visibilityChanged(false);
}

QString MidiActivityWindow::cellText(int part, int column) const
{
    const QTableWidgetItem* item = _parts->item(part, column);
    return item ? item->text() : QString();
}

void MidiActivityWindow::refresh()
{
    Emulator* emulator = _binding ? _binding->emulator() : nullptr;
    const StateNode midi = DeviceState::Midi(emulator ? emulator->GetContext() : nullptr);
    const StateNode* available = midi.find("available");
    const bool ok = available != nullptr && available->b;
    _panic->setEnabled(ok);
    std::array<std::array<uint64_t, 2>, 16> keys{};
    if (!ok)
    {
        _summary->setText(Q(Text(midi.find("description"))));
        for (int part = 0; part < 16; part++)
            for (int column = 0; column < ColCount; column++)
                _parts->item(part, column)->setText(QString());
        _strip->setKeys(keys);
        return;
    }
    const StateNode* bank = midi.find("bank");
    const StateNode* counters = midi.find("counters");
    const StateNode* line = midi.find("line");
    _summary->setText(tr("%1 in %2 - bank %3%4. Voices %5 of %6, fading %7. Line: %8 edges; %9 bytes, %10 framing "
                         "errors, %11 dropped")
                          .arg(Q(Text(midi.find("synthesizer"))), Q(Text(midi.find("slot"))), Q(Text(bank->find("status"))),
                               Text(bank->find("name")).empty() ? QString() : QString(" (%1)").arg(Q(Text(bank->find("name")))))
                          .arg(Int(midi.find("active_voices")))
                          .arg(Int(midi.find("polyphony_limit")))
                          .arg(Int(midi.find("fading_voices")))
                          .arg(Int(line->find("edges")))
                          .arg(Int(counters->find("bytes_received")))
                          .arg(Int(counters->find("framing_errors")))
                          .arg(Int(counters->find("dropped_busy")) + Int(counters->find("dropped_queue_full"))));
    int part = 0;
    for (const StateNode& p : midi.find("parts")->items)
    {
        if (part >= 16)
            break;
        QStringList notes;
        for (const StateNode& note : p.find("notes")->items)
            notes << Q(note.s);
        for (const StateNode& key : p.find("keys")->items)
        {
            const int k = static_cast<int>(key.i);
            if (k >= 0 && k < 128)
                keys[size_t(part)][size_t(k >> 6)] |= uint64_t{1} << (k & 63);
        }
        _parts->item(part, ColChannel)->setText(QString::number(Int(p.find("channel"))));
        _parts->item(part, ColProgram)->setText(QString::number(Int(p.find("program"))) +
                                                (p.find("rhythm") && p.find("rhythm")->b ? tr(" (drums)") : QString()));
        _parts->item(part, ColPreset)->setText(Q(Text(p.find("preset"))));
        _parts->item(part, ColVolume)->setText(QString::number(Int(p.find("volume"))));
        _parts->item(part, ColPan)->setText(QString::number(Int(p.find("pan"))));
        _parts->item(part, ColVoices)->setText(QString::number(Int(p.find("active_voices"))));
        _parts->item(part, ColNotes)->setText(notes.join(" "));
        part++;
    }
    _strip->setKeys(keys);
}

void MidiActivityWindow::panic()
{
    Emulator* emulator = _binding ? _binding->emulator() : nullptr;
    if (!emulator)
        return;
    const MidiControlReply reply = MidiControl::Execute(emulator->GetContext(), "panic");
    _panicResult->setText(Q(reply.message));
}
