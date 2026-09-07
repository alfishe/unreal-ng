#pragma once
//
// main_window.h — QMainWindow composing all PoC widgets.
//

#include <QMainWindow>
#include <QLabel>
#include <QListView>
#include <memory>
#include "../ttd/ttd_reader.h"
#include "../ttd/ttd_materialize.h"
#include "../model/marks_model.h"
#include "screen_widget.h"
#include "timeline_widget.h"

namespace ttd {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    /// Load a .ttd file. Returns false on error.
    bool loadTtd(const QString& path);

    /// Load a .marks.json sidecar.
    bool loadMarks(const QString& path);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onOpenFile();
    void onOpenMarks();
    void onFrameChanged(uint64_t frame);
    void onUserMarkRequested(uint64_t frame);
    void onZoomIn();
    void onZoomOut();
    void onToggleMarks();

private:
    void setupUi();
    void setupMenuBar();
    void setupToolBar();
    void updateInfoLabels();
    void renderCurrentCheckpoint();

    // Data
    TtdDump _dump;
    Materializer _materializer;
    MarksModel _marksModel;

    // Widgets
    ScreenWidget*   _screenWidget = nullptr;
    TimelineWidget* _timeline     = nullptr;
    QListView*      _marksList    = nullptr;
    QLabel*         _sessionLabel = nullptr;
    QLabel*         _posLabel     = nullptr;
    QLabel*         _statusLabel  = nullptr;

    // State
    bool _marksVisible = true;
    int  _currentCpIndex = -1;
};

} // namespace ttd
