#include "multitrackdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QLabel>

MultiTrackDialog::MultiTrackDialog(QWidget* parent)
    : QDialog(parent)
{
    createUI();
    setWindowTitle(tr("Multi-Track Audio Configuration"));
    setMinimumSize(700, 350);
}

MultiTrackDialog::~MultiTrackDialog() = default;

void MultiTrackDialog::createUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Description
    auto* descLabel = new QLabel(tr("Configure individual audio tracks for multi-track recording.\n"
                                     "Each track captures a separate audio source."), this);
    mainLayout->addWidget(descLabel);

    // Table
    _table = new QTableWidget(this);
    _table->setColumnCount(7);
    _table->setHorizontalHeaderLabels({
        tr("Enabled"), tr("Name"), tr("Source"), tr("Codec"),
        tr("Bitrate\n(kbps)"), tr("Volume"), tr("Pan")
    });
    _table->horizontalHeader()->setStretchLastSection(true);
    _table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(_table);

    // Buttons row
    auto* buttonLayout = new QHBoxLayout();

    _addButton = new QPushButton(tr("+ Add Track"), this);
    _removeButton = new QPushButton(tr("- Remove"), this);
    _duplicateButton = new QPushButton(tr("Duplicate"), this);
    _clearButton = new QPushButton(tr("Clear All"), this);

    buttonLayout->addWidget(_addButton);
    buttonLayout->addWidget(_removeButton);
    buttonLayout->addWidget(_duplicateButton);
    buttonLayout->addWidget(_clearButton);
    buttonLayout->addStretch();

    mainLayout->addLayout(buttonLayout);

    // OK / Cancel
    auto* dialogLayout = new QHBoxLayout();
    dialogLayout->addStretch();
    _okButton = new QPushButton(tr("OK"), this);
    _cancelButton = new QPushButton(tr("Cancel"), this);
    _okButton->setDefault(true);
    dialogLayout->addWidget(_okButton);
    dialogLayout->addWidget(_cancelButton);
    mainLayout->addLayout(dialogLayout);

    // Connections
    connect(_addButton, &QPushButton::clicked, this, &MultiTrackDialog::onAddTrack);
    connect(_removeButton, &QPushButton::clicked, this, &MultiTrackDialog::onRemoveTrack);
    connect(_duplicateButton, &QPushButton::clicked, this, &MultiTrackDialog::onDuplicateTrack);
    connect(_clearButton, &QPushButton::clicked, this, &MultiTrackDialog::onClearAll);
    connect(_table, &QTableWidget::cellChanged, this, &MultiTrackDialog::onCellChanged);
    connect(_okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(_cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    updateButtonStates();
}

void MultiTrackDialog::populateSourceCombo(QComboBox* combo)
{
    combo->addItem(sourceToString(AudioSourceType::MasterMix), static_cast<int>(AudioSourceType::MasterMix));
    combo->addItem(sourceToString(AudioSourceType::Beeper), static_cast<int>(AudioSourceType::Beeper));
    combo->addItem(sourceToString(AudioSourceType::AY1_All), static_cast<int>(AudioSourceType::AY1_All));
    combo->addItem(sourceToString(AudioSourceType::AY2_All), static_cast<int>(AudioSourceType::AY2_All));
    combo->addItem(sourceToString(AudioSourceType::AY3_All), static_cast<int>(AudioSourceType::AY3_All));
    combo->addItem(sourceToString(AudioSourceType::COVOX), static_cast<int>(AudioSourceType::COVOX));
    combo->addItem(sourceToString(AudioSourceType::GeneralSound), static_cast<int>(AudioSourceType::GeneralSound));
    combo->addItem(sourceToString(AudioSourceType::Moonsound), static_cast<int>(AudioSourceType::Moonsound));
    combo->addItem(sourceToString(AudioSourceType::AY1_ChannelA), static_cast<int>(AudioSourceType::AY1_ChannelA));
    combo->addItem(sourceToString(AudioSourceType::AY1_ChannelB), static_cast<int>(AudioSourceType::AY1_ChannelB));
    combo->addItem(sourceToString(AudioSourceType::AY1_ChannelC), static_cast<int>(AudioSourceType::AY1_ChannelC));
    combo->addItem(sourceToString(AudioSourceType::AY2_ChannelA), static_cast<int>(AudioSourceType::AY2_ChannelA));
    combo->addItem(sourceToString(AudioSourceType::AY2_ChannelB), static_cast<int>(AudioSourceType::AY2_ChannelB));
    combo->addItem(sourceToString(AudioSourceType::AY2_ChannelC), static_cast<int>(AudioSourceType::AY2_ChannelC));
    combo->addItem(sourceToString(AudioSourceType::AY3_ChannelA), static_cast<int>(AudioSourceType::AY3_ChannelA));
    combo->addItem(sourceToString(AudioSourceType::AY3_ChannelB), static_cast<int>(AudioSourceType::AY3_ChannelB));
    combo->addItem(sourceToString(AudioSourceType::AY3_ChannelC), static_cast<int>(AudioSourceType::AY3_ChannelC));
}

void MultiTrackDialog::populateCodecCombo(QComboBox* combo)
{
    combo->addItem("aac");
    combo->addItem("mp3");
    combo->addItem("flac");
    combo->addItem("pcm_s16le");
    combo->addItem("opus");
    combo->addItem("vorbis");
}

QString MultiTrackDialog::sourceToString(AudioSourceType source)
{
    switch (source)
    {
        case AudioSourceType::MasterMix:      return "Master Mix";
        case AudioSourceType::Beeper:          return "Beeper";
        case AudioSourceType::AY1_All:         return "AY-1 (All)";
        case AudioSourceType::AY2_All:         return "AY-2 (All)";
        case AudioSourceType::AY3_All:         return "AY-3 (All)";
        case AudioSourceType::COVOX:           return "COVOX/DAC";
        case AudioSourceType::GeneralSound:    return "General Sound";
        case AudioSourceType::Moonsound:       return "Moonsound";
        case AudioSourceType::AY1_ChannelA:    return "AY-1 Ch.A";
        case AudioSourceType::AY1_ChannelB:    return "AY-1 Ch.B";
        case AudioSourceType::AY1_ChannelC:    return "AY-1 Ch.C";
        case AudioSourceType::AY2_ChannelA:    return "AY-2 Ch.A";
        case AudioSourceType::AY2_ChannelB:    return "AY-2 Ch.B";
        case AudioSourceType::AY2_ChannelC:    return "AY-2 Ch.C";
        case AudioSourceType::AY3_ChannelA:    return "AY-3 Ch.A";
        case AudioSourceType::AY3_ChannelB:    return "AY-3 Ch.B";
        case AudioSourceType::AY3_ChannelC:    return "AY-3 Ch.C";
        case AudioSourceType::Custom:          return "Custom";
        default:                                return "Unknown";
    }
}

AudioSourceType MultiTrackDialog::stringToSource(const QString& str)
{
    if (str == "Master Mix") return AudioSourceType::MasterMix;
    if (str == "Beeper") return AudioSourceType::Beeper;
    if (str == "AY-1 (All)") return AudioSourceType::AY1_All;
    if (str == "AY-2 (All)") return AudioSourceType::AY2_All;
    if (str == "AY-3 (All)") return AudioSourceType::AY3_All;
    if (str == "COVOX/DAC") return AudioSourceType::COVOX;
    if (str == "General Sound") return AudioSourceType::GeneralSound;
    if (str == "Moonsound") return AudioSourceType::Moonsound;
    if (str == "AY-1 Ch.A") return AudioSourceType::AY1_ChannelA;
    if (str == "AY-1 Ch.B") return AudioSourceType::AY1_ChannelB;
    if (str == "AY-1 Ch.C") return AudioSourceType::AY1_ChannelC;
    if (str == "AY-2 Ch.A") return AudioSourceType::AY2_ChannelA;
    if (str == "AY-2 Ch.B") return AudioSourceType::AY2_ChannelB;
    if (str == "AY-2 Ch.C") return AudioSourceType::AY2_ChannelC;
    if (str == "AY-3 Ch.A") return AudioSourceType::AY3_ChannelA;
    if (str == "AY-3 Ch.B") return AudioSourceType::AY3_ChannelB;
    if (str == "AY-3 Ch.C") return AudioSourceType::AY3_ChannelC;
    return AudioSourceType::MasterMix;
}

void MultiTrackDialog::setTracks(const std::vector<AudioTrackConfig>& tracks)
{
    _updating = true;

    _table->setRowCount(static_cast<int>(tracks.size()));

    for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
    {
        const auto& track = tracks[i];

        // Enabled checkbox
        auto* enabledCheck = new QCheckBox();
        enabledCheck->setChecked(track.enabled);
        _table->setCellWidget(i, 0, enabledCheck);

        // Name
        _table->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(track.name)));

        // Source combo
        auto* sourceCombo = new QComboBox();
        populateSourceCombo(sourceCombo);
        int sourceIdx = sourceCombo->findData(static_cast<int>(track.source));
        if (sourceIdx >= 0) sourceCombo->setCurrentIndex(sourceIdx);
        _table->setCellWidget(i, 2, sourceCombo);

        // Codec combo
        auto* codecCombo = new QComboBox();
        populateCodecCombo(codecCombo);
        int codecIdx = codecCombo->findText(QString::fromStdString(track.codec));
        if (codecIdx >= 0) codecCombo->setCurrentIndex(codecIdx);
        _table->setCellWidget(i, 3, codecCombo);

        // Bitrate spin
        auto* bitrateSpin = new QSpinBox();
        bitrateSpin->setRange(32, 1024);
        bitrateSpin->setValue(static_cast<int>(track.bitrate));
        bitrateSpin->setSuffix(" kbps");
        _table->setCellWidget(i, 4, bitrateSpin);

        // Volume slider (0-100, representing 0.0-1.0)
        auto* volumeSlider = new QSlider(Qt::Horizontal);
        volumeSlider->setRange(0, 100);
        volumeSlider->setValue(static_cast<int>(track.volume * 100));
        _table->setCellWidget(i, 5, volumeSlider);

        // Pan slider (-100 to +100)
        auto* panSlider = new QSlider(Qt::Horizontal);
        panSlider->setRange(-100, 100);
        panSlider->setValue(track.pan);
        _table->setCellWidget(i, 6, panSlider);
    }

    _updating = false;
    updateButtonStates();
}

std::vector<AudioTrackConfig> MultiTrackDialog::getTracks() const
{
    std::vector<AudioTrackConfig> result;

    for (int i = 0; i < _table->rowCount(); ++i)
    {
        AudioTrackConfig track;

        auto* enabledCheck = qobject_cast<QCheckBox*>(_table->cellWidget(i, 0));
        if (enabledCheck) track.enabled = enabledCheck->isChecked();

        auto* nameItem = _table->item(i, 1);
        if (nameItem) track.name = nameItem->text().toStdString();

        auto* sourceCombo = qobject_cast<QComboBox*>(_table->cellWidget(i, 2));
        if (sourceCombo) track.source = static_cast<AudioSourceType>(sourceCombo->currentData().toInt());

        auto* codecCombo = qobject_cast<QComboBox*>(_table->cellWidget(i, 3));
        if (codecCombo) track.codec = codecCombo->currentText().toStdString();

        auto* bitrateSpin = qobject_cast<QSpinBox*>(_table->cellWidget(i, 4));
        if (bitrateSpin) track.bitrate = static_cast<uint32_t>(bitrateSpin->value());

        auto* volumeSlider = qobject_cast<QSlider*>(_table->cellWidget(i, 5));
        if (volumeSlider) track.volume = volumeSlider->value() / 100.0f;

        auto* panSlider = qobject_cast<QSlider*>(_table->cellWidget(i, 6));
        if (panSlider) track.pan = panSlider->value();

        result.push_back(track);
    }

    return result;
}

void MultiTrackDialog::onAddTrack()
{
    _updating = true;
    int row = _table->rowCount();
    _table->setRowCount(row + 1);

    AudioTrackConfig newTrack;
    newTrack.name = "Track " + std::to_string(row + 1);
    newTrack.source = AudioSourceType::MasterMix;
    newTrack.codec = "aac";
    newTrack.bitrate = 192;

    auto* enabledCheck = new QCheckBox();
    enabledCheck->setChecked(newTrack.enabled);
    _table->setCellWidget(row, 0, enabledCheck);

    _table->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(newTrack.name)));

    auto* sourceCombo = new QComboBox();
    populateSourceCombo(sourceCombo);
    _table->setCellWidget(row, 2, sourceCombo);

    auto* codecCombo = new QComboBox();
    populateCodecCombo(codecCombo);
    codecCombo->setCurrentText(QString::fromStdString(newTrack.codec));
    _table->setCellWidget(row, 3, codecCombo);

    auto* bitrateSpin = new QSpinBox();
    bitrateSpin->setRange(32, 1024);
    bitrateSpin->setValue(static_cast<int>(newTrack.bitrate));
    bitrateSpin->setSuffix(" kbps");
    _table->setCellWidget(row, 4, bitrateSpin);

    auto* volumeSlider = new QSlider(Qt::Horizontal);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(100);
    _table->setCellWidget(row, 5, volumeSlider);

    auto* panSlider = new QSlider(Qt::Horizontal);
    panSlider->setRange(-100, 100);
    panSlider->setValue(0);
    _table->setCellWidget(row, 6, panSlider);

    _updating = false;
    _table->selectRow(row);
    updateButtonStates();
}

void MultiTrackDialog::onRemoveTrack()
{
    int row = _table->currentRow();
    if (row >= 0)
    {
        _table->removeRow(row);
        updateButtonStates();
    }
}

void MultiTrackDialog::onDuplicateTrack()
{
    int row = _table->currentRow();
    if (row < 0) return;

    auto tracks = getTracks();
    if (row >= static_cast<int>(tracks.size())) return;

    AudioTrackConfig dup = tracks[row];
    dup.name += " (copy)";
    tracks.insert(tracks.begin() + row + 1, dup);

    setTracks(tracks);
    _table->selectRow(row + 1);
}

void MultiTrackDialog::onClearAll()
{
    auto reply = QMessageBox::question(this, tr("Clear All Tracks"),
                                        tr("Remove all audio tracks?"),
                                        QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes)
    {
        _table->setRowCount(0);
        updateButtonStates();
    }
}

void MultiTrackDialog::onCellChanged(int row, int col)
{
    (void)row;
    (void)col;
    // Currently no special handling needed; data is read on OK
}

void MultiTrackDialog::updateButtonStates()
{
    int rowCount = _table->rowCount();
    int currentRow = _table->currentRow();

    _removeButton->setEnabled(currentRow >= 0);
    _duplicateButton->setEnabled(currentRow >= 0);
    _clearButton->setEnabled(rowCount > 0);
}
