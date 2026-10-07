#pragma once

/// @file flattendialog.h
/// @brief The strategy dialog for a composite's unsaved writes (DT-9, interactive): the guest's changes, the four
/// strategies (S1 one image, S2 keep the session, S3 commit into the base image, S4 write back into the folders)
/// with the ones that do not apply greyed out and why, `writes.save` preselected, a plan preview, and the run.
/// The choices come from flattenchoice.h (Qt-free); the requests go through `run`, the panel's MediaControl call.
/// Design: docs/inprogress/2026-10-05-media-multisource/phases/c8-commit-writeback.md (C8c).

#include <QDialog>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"
#include "media/core/flattenchoice.h"

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;

class FlattenDialog : public QDialog
{
    Q_OBJECT

public:
    /// verb, path, options -> the MediaControl reply (the panel's `run` without the disposition question)
    using Runner = std::function<StateNode(const std::string& verb, const std::string& path, const std::map<std::string, std::string>& options)>;

    FlattenDialog(const std::string& slot, Runner runner, const QString& lastDirectory, QWidget* parent = nullptr);

    /// The reply of the strategy that ran (valid after accept())
    const StateNode& Result() const { return _result; }

private:
    FlattenChoice Choice() const;
    void UpdateControls();
    void Preview();
    void RunChosen();

    std::string _slot;
    Runner _run;
    std::vector<FlattenOption> _options;
    std::vector<QRadioButton*> _radios;
    QLineEdit* _path = nullptr;
    QPushButton* _browse = nullptr;
    QCheckBox* _compact = nullptr;
    QCheckBox* _keepBoth = nullptr;
    QCheckBox* _force = nullptr;
    QPlainTextEdit* _text = nullptr;
    QPushButton* _preview = nullptr;
    QString _lastDirectory;
    StateNode _result;
};
