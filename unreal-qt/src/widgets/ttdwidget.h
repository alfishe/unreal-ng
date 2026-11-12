/**
 * @file ttdwidget.h
 * @brief Time Travel Debugging (TTD) control panel & timeline scrubber widget.
 */

#pragma once

#include <functional>

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>

#include "widgets/branchlanes.h"
#include "widgets/journalspanslider.h"
#include <QScrollArea>
#include <set>
#include <utility>
#include <vector>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <map>
#include <memory>
#include <string>

class Emulator;
namespace ttd
{
struct TTDSessionInfo;
}


/**
 * @brief TtdWidget - Time Travel Debugging control panel & timeline scrubber.
 *
 * Provides GUI controls for recording, loading/exporting .ttd files, clearing session history,
 * and interactive timeline scrubbing with immediate machine state repositioning & screen refresh.
 */
class TtdWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TtdWidget(QWidget* parent = nullptr);
    virtual ~TtdWidget() override = default;

    /// Update enabled state, status labels, and slider bounds from active emulator
    void updateState(std::shared_ptr<Emulator> activeEmulator);

    /// User preference / action toggle for widget visibility
    void setVisibleByUser(bool visible);
    bool isVisibleByUser() const { return _visibleByUser; }

    /// Exact height requested by the widget depending on user visibility and scrubber row state.
    int desiredHeight() const;
    int singleRowHeight() const;
    int doubleRowHeight() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    /// Asks the user before a branch is deleted (title, question; true = go ahead). A message box by default; the
    /// tests answer without one
    std::function<bool(const QString&, const QString&)> confirm;

signals:
    void visibilityChanged(bool visible);
    void heightChanged();
    void recordingStateChanged(bool isRecording);
    /// The machine moved in time (a seek, a load): the window repaints its viewport
    void viewportRefreshRequested();

public slots:
    void onRecordToggled();
    void onLoadSession();
    void onExportSession();
    void onClearSession();
    void onJumpStart();
    void onStepBack();
    void onStepForward();
    void onJumpEnd();
    void onResumeFromHere();
    void onSliderValueChanged(int value);
    void onSliderMoved(int value);
    /// The Advanced row's "Write journal" box: switch the write journal at any moment (remembered)
    void onJournalToggled(bool on);
    /// Show or hide the Advanced row (remembered)
    void onAdvancedToggled(bool open);
    /// Show or hide the Branches row (remembered; the button shows only once the session has branches)
    void onBranchesToggled(bool open);
    /// The Branches row's actions on the selected branch (the lanes' selection, else the active one)
    void onSwitchBranch();
    void onRenameBranch();
    void onTagBranch();
    void onNoteBranch();
    void onPinBranch();
    void onDeleteBranch();
    void onSaveLineage();


private:
    void updateTelemetry();
    /// The history combo's limit onto the active emulator's TTD (a new one, or a change)
    void applyHistoryLimit();
    /// The saved journal choice onto the active emulator's TTD while it holds no session (a recording
    /// already under way keeps what it records with)
    void applyJournalPreference();
    /// Height of the visible rows and the spacing between them
    int rowsHeight(bool advanced, bool scrubber, bool branches = false) const;
    /// The Branches row, the button, the slider's branch tints and fork marks from the published branch list
    void updateBranches(const ttd::TTDSessionInfo& info, uint64_t current);
    /// The branch the row's actions act on: the lanes' selection, else the active branch
    uint32_t chosenBranch() const;
    /// A branch verb on the active emulator; a refusal is shown. Returns the reply's success
    bool runBranchVerb(const std::string& verb, std::map<std::string, std::string> options);
    void performSeekToFrame(uint64_t targetFrame, bool frameStart = false);
    /// Build the write journal for frames from..to by replay on a worker thread,
    /// with a progress dialog that can cancel it
    void buildJournal(uint64_t fromFrame, uint64_t toFrame);

    std::shared_ptr<Emulator> _activeEmulator = nullptr;
    QTimer* _telemetryTimer = nullptr;

    bool _visibleByUser = false;
    bool _isInternalSliderUpdate = false;
    bool _lastIsRecording = false;
    bool _journalBuildRunning = false;   ///< a build runs on a worker thread: leave the session alone

    // Control Row (Top)
    QWidget* _controlContainer = nullptr;
    QPushButton* _recordBtn = nullptr;
    QPushButton* _loadBtn = nullptr;
    QPushButton* _exportBtn = nullptr;
    QPushButton* _clearBtn = nullptr;
    QLabel* _statusLabel = nullptr;
    QToolButton* _advancedBtn = nullptr;   // shows the Advanced row
    QToolButton* _branchesBtn = nullptr;   // shows the Branches row; there only once the session has branches
    QToolButton* _closeBtn = nullptr;

    // Advanced Row (hidden by default; its settings and whether it is open are saved)
    QWidget* _advancedContainer = nullptr;
    QCheckBox* _journalCheck = nullptr;   // write journal on / off (D40); on by default in the panel
    QComboBox* _historyCombo = nullptr;   // history limit (bytes, 0 = all)

    // Branches Row (TTD branches, surfaces.md §1): the lanes and the actions on the chosen branch
    QWidget* _branchesContainer = nullptr;
    QScrollArea* _branchesScroll = nullptr;
    BranchLanes* _branchLanes = nullptr;
    QPushButton* _branchSwitchBtn = nullptr;
    QPushButton* _branchRenameBtn = nullptr;
    QPushButton* _branchTagsBtn = nullptr;
    QPushButton* _branchNoteBtn = nullptr;
    QPushButton* _branchPinBtn = nullptr;
    QPushButton* _branchDeleteBtn = nullptr;
    QPushButton* _branchSaveBtn = nullptr;
    int _branchButtonsHeight = 26;
    std::set<uint32_t> _knownBranches;    ///< the ids seen: a new one is a fork the status line names
    QString _forkNotice;                  ///< "previous future kept as ...", for a few seconds
    qint64 _forkNoticeUntil = 0;          ///< ms since epoch

    // Scrubber Row Container (Bottom)
    QWidget* _scrubberContainer = nullptr;
    QPushButton* _jumpStartBtn = nullptr;
    QPushButton* _stepBackBtn = nullptr;
    JournalSpanSlider* _timelineSlider = nullptr;
    QPushButton* _buildJournalBtn = nullptr;   // build the write journal by replay (menu: whole / to here / from here)
    QPushButton* _stepForwardBtn = nullptr;
    QPushButton* _jumpEndBtn = nullptr;
    QPushButton* _resumeFromHereBtn = nullptr;
};
