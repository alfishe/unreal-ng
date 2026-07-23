#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <vector>

#include "emulator/recording/recordingmanager.h"

/// @brief Multi-track audio configuration dialog
///
/// Table-based track editor for configuring multiple audio sources
/// in a recording. Each track has: enabled flag, name, source, codec,
/// bitrate, volume, and pan.
class MultiTrackDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MultiTrackDialog(QWidget* parent = nullptr);
    ~MultiTrackDialog() override;

    /// Set the current track list for editing
    void setTracks(const std::vector<AudioTrackConfig>& tracks);

    /// Get the configured track list
    std::vector<AudioTrackConfig> getTracks() const;

private slots:
    void onAddTrack();
    void onRemoveTrack();
    void onDuplicateTrack();
    void onClearAll();
    void onCellChanged(int row, int col);

private:
    void createUI();
    void populateSourceCombo(QComboBox* combo);
    void populateCodecCombo(QComboBox* combo);
    void updateButtonStates();

    static QString sourceToString(AudioSourceType source);
    static AudioSourceType stringToSource(const QString& str);

    QTableWidget* _table = nullptr;
    QPushButton* _addButton = nullptr;
    QPushButton* _removeButton = nullptr;
    QPushButton* _duplicateButton = nullptr;
    QPushButton* _clearButton = nullptr;
    QPushButton* _okButton = nullptr;
    QPushButton* _cancelButton = nullptr;

    bool _updating = false;  ///< Guard against recursive cellChanged signals
};
