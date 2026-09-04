/**
 * @file tapemanagerwindow.h
 * @brief TapeManagerWindow — top-level non-modal Tape Manager window
 *        (design §9.1): toolbar + fast-load badge + progress + block table +
 *        details pane.
 *
 * Owns nothing core: state arrives exclusively through
 * EmulatorBinding::tapeStateChanged snapshots; transport commands leave
 * exclusively through the binding's bracketed tape* methods. One instance per
 * app session, floating top-level, never modal (design §9.4).
 */

#pragma once

#include <QLabel>
#include <QProgressBar>
#include <QTableView>
#include <QWidget>

#include "tape/tapeblocktablemodel.h"
#include "tape/tapeuisnapshot.h"

class EmulatorBinding;
class QToolButton;

class TapeManagerWindow : public QWidget
{
    Q_OBJECT

public:
    explicit TapeManagerWindow(QWidget* parent = nullptr);

    /// Connect to the central binding (mirrors DebuggerWindow::setBinding).
    /// Safe to call once at startup; the window re-enables on bound().
    void setBinding(EmulatorBinding* binding);

    /// Clear to the unbound placeholder (mirrors DebuggerWindow::reset).
    void reset();

signals:
    /// Visibility changed via the window's own close box — keeps the Tools
    /// menu action in sync when the user closes the window directly.
    void visibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onTapeSnapshot(const TapeUiSnapshot& snapshot);
    void onBound();
    void onUnbound();
    void onCurrentRowChanged(const QModelIndex& current, const QModelIndex& previous);
    void onBlockDoubleClicked(const QModelIndex& index);
    void onTableContextMenu(const QPoint& pos);

private:
    void buildUi();
    void rebuildFromSnapshot(const TapeUiSnapshot& snapshot);
    size_t currentBlockIndex() const;  // r10 prev/next anchor (in-flight block, else cursor)
    void updateBadge();
    void updateProgress();
    void updateToolbarEnabledState();
    void updateDetails();
    void updateWindowTitle();
    void openBlockDialog(int row);

    EmulatorBinding* _binding = nullptr;  // central state binding (not owned)

    TapeBlockTableModel* _model = nullptr;
    QToolButton* _playButton = nullptr;
    QToolButton* _pauseButton = nullptr;
    QToolButton* _stopButton = nullptr;
    QToolButton* _rewindButton = nullptr;
    QToolButton* _prevBlockButton = nullptr;  // r10: seek one block back
    QToolButton* _nextBlockButton = nullptr;  // r10: seek one block forward
    QLabel* _blockCountLabel = nullptr;
    QLabel* _badgeLabel = nullptr;
    QProgressBar* _progressBar = nullptr;
    QLabel* _progressLabel = nullptr;
    QTableView* _table = nullptr;
    QLabel* _detailsLabel = nullptr;

    TapeUiSnapshot _lastSnapshot;   // everything the refresh paths need
    bool _hasSnapshot = false;
    int _detailsRow = -1;           // row shown in the details pane

    // Generation-scoped fields cached from the last catalogChanged snapshot.
    // The producer ships them ONCE per image (design §9.3 rule 3); per-tick
    // snapshots leave them defaulted, so every refresh path must read the
    // catalog/plan/format from this cache, never from _lastSnapshot (r7 fix:
    // the per-tick defaults used to flip the badge to "unreadable" right
    // after every successful load, with the table still populated).
    bool _hasGenerationSnapshot = false;
    bool _catalogValid = false;
    size_t _catalogSize = 0;
    QString _formatId;
    TapeFastLoadPlan _plan;
};
