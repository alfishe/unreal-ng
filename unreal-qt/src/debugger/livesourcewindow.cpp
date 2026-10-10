#include "livesourcewindow.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTextBlock>
#include <QTimer>
#include <QVBoxLayout>

#include "debugger/asm/asmcontrol.h"
#include "debugger/asm/sync/asmsyncservice.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"

namespace
{
const StateNode* Member(const StateNode& node, const char* name)
{
    return node.find(name);
}

std::string Text(const StateNode& node, const char* name)
{
    const StateNode* n = node.find(name);
    return n && n->kind == StateNode::Kind::String ? n->s : std::string();
}

int64_t Number(const StateNode& node, const char* name, int64_t otherwise = 0)
{
    const StateNode* n = node.find(name);
    return n && n->kind == StateNode::Kind::Int ? n->i : otherwise;
}
}  // namespace

LiveSourceWindow::LiveSourceWindow(Emulator* emulator, QWidget* parent) : QWidget(parent, Qt::Window), _emulatorId(emulator ? emulator->GetUUID().toString() : std::string())
{
    setWindowTitle(tr("Live Source"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(760, 640);

    _status = new QLabel(this);
    _status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    _text = new QPlainTextEdit(this);
    _text->setReadOnly(true);
    _text->setLineWrapMode(QPlainTextEdit::NoWrap);
    _text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    _text->setPlaceholderText(tr("No source yet: start an assembler (ALASM, TASM 4.12) in the machine and load or type a text"));

    _hints = new QListWidget(this);
    _hints->setToolTip(tr("The host build's errors and warnings; double-click one to see its line"));

    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(_text);
    splitter->addWidget(_hints);
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);

    _watch = new QPushButton(tr("&Watch"), this);
    _watch->setCheckable(true);
    _watch->setToolTip(tr("Follow the source while you type: half a second after a pause it is built on the host, its "
                          "labels show in the debugger (set live:sync:<assembler>)"));
    _extract = new QPushButton(tr("&Extract..."), this);
    _extract->setToolTip(tr("Save the source as the assembler's own file (its type) or as text"));
    _convert = new QPushButton(tr("&Convert..."), this);
    _convert->setToolTip(tr("Save the source in another dialect (sjasmplus, pasmo, z88dk ...)"));

    auto* buttons = new QHBoxLayout();
    buttons->addWidget(_watch);
    buttons->addWidget(_extract);
    buttons->addWidget(_convert);
    buttons->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(_status);
    layout->addWidget(splitter, 1);
    layout->addLayout(buttons);

    connect(_watch, &QPushButton::toggled, this, &LiveSourceWindow::toggleWatch);
    connect(_extract, &QPushButton::clicked, this, &LiveSourceWindow::extract);
    connect(_convert, &QPushButton::clicked, this, &LiveSourceWindow::convert);
    connect(_hints, &QListWidget::itemDoubleClicked, this, &LiveSourceWindow::goToHint);

    _timer = new QTimer(this);
    _timer->setInterval(300);
    connect(_timer, &QTimer::timeout, this, &LiveSourceWindow::refresh);
    _timer->start();

    startWatch();
    refresh();
}

LiveSourceWindow::~LiveSourceWindow()
{
    stopOwnWatch();
}

void LiveSourceWindow::setEmulator(Emulator* emulator)
{
    const std::string id = emulator ? emulator->GetUUID().toString() : std::string();
    if (id == _emulatorId)
        return;
    stopOwnWatch();
    _emulatorId = id;
    _shownGeneration = 0;
    _shownCursor = -2;
    _text->clear();
    _hints->clear();
    _hintLines.clear();
    _errors = 0;
    _warnings = 0;
    startWatch();
    refresh();
}

std::shared_ptr<Emulator> LiveSourceWindow::instance() const
{
    return _emulatorId.empty() ? nullptr : EmulatorManager::GetInstance()->GetEmulator(_emulatorId);
}

AsmSyncService* LiveSourceWindow::service(const std::shared_ptr<Emulator>& emulator) const
{
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    return context && context->pDebugManager ? context->pDebugManager->GetAsmSyncService() : nullptr;
}

void LiveSourceWindow::startWatch()
{
    const std::shared_ptr<Emulator> emulator = instance();
    AsmSyncService* sync = service(emulator);
    if (sync && !sync->Watching())
    {
        sync->Start({});
        _ownWatch = true;
    }
}

void LiveSourceWindow::stopOwnWatch()
{
    const std::shared_ptr<Emulator> emulator = instance();
    if (AsmSyncService* sync = service(emulator); sync && _ownWatch)
        sync->Stop();
    _ownWatch = false;
}

void LiveSourceWindow::toggleWatch(bool on)
{
    const std::shared_ptr<Emulator> emulator = instance();
    AsmSyncService* sync = service(emulator);
    if (!sync)
        return;
    if (on && !sync->Watching())
    {
        sync->Start({});
        _ownWatch = true;
    }
    else if (!on && sync->Watching())
    {
        sync->Stop();
        _ownWatch = false;
    }
    refresh();
}

void LiveSourceWindow::refresh()
{
    const std::shared_ptr<Emulator> emulator = instance();
    AsmSyncService* sync = service(emulator);
    if (!sync)
    {
        _status->setText(tr("No emulator instance"));
        return;
    }
    const StateNode status = sync->StatusValue();
    const StateNode hints = sync->HintsValue();
    const bool watching = Member(status, "watching") && Member(status, "watching")->b;
    {
        const QSignalBlocker blocker(_watch);
        _watch->setChecked(watching);
    }

    // The text and hints of a newer build
    uint64_t generation = 0;
    const std::string text = sync->LastText(generation);
    if (generation != _shownGeneration)
    {
        const int top = _text->cursorForPosition(QPoint(0, 0)).blockNumber();
        _text->setPlainText(QString::fromStdString(text));
        if (QTextBlock block = _text->document()->findBlockByNumber(top); block.isValid())
            _text->setTextCursor(QTextCursor(block));
        _hints->clear();
        _hintLines.clear();
        _errors = 0;
        _warnings = 0;
        if (const StateNode* list = Member(hints, "hints"))
            for (const StateNode& h : list->items)
            {
                const std::string severity = Text(h, "severity");
                if (severity == "info")
                    continue;
                (severity == "error" ? _errors : _warnings)++;
                const int line = static_cast<int>(Number(h, "line"));
                auto* item = new QListWidgetItem(QString("%1  %2").arg(line > 0 ? tr("line %1").arg(line) : QString("-"), QString::fromStdString(Text(h, "message"))));
                item->setData(Qt::UserRole, line);
                item->setForeground(severity == "error" ? QColor(0xB0, 0x20, 0x20) : QColor(0x90, 0x60, 0x00));
                _hints->addItem(item);
                if (line > 0)
                    _hintLines.append({line, QString::fromStdString(severity)});
            }
        _shownGeneration = generation;
        _shownCursor = -2;
    }

    // The guest's cursor line (TASM's editor tells it)
    const StateNode* textState = Member(status, "text");
    const int cursor = textState ? static_cast<int>(Number(*textState, "current_line", -1)) : -1;
    if (cursor != _shownCursor)
    {
        highlight(cursor);
        _shownCursor = cursor;
    }

    // The status line
    const std::string assembler = Text(status, "assembler");
    QString line = !watching ? tr("Not watching") : assembler.empty() ? tr("Watching: no known assembler in RAM yet")
                                                                       : tr("Watching %1").arg(QString::fromStdString(textState ? Text(*textState, "title") : assembler));
    if (generation > 0)
    {
        line += tr("  ·  build %1: %2 labels, %3 errors, %4 warnings")
                    .arg(generation)
                    .arg(Number(hints, "labels"))
                    .arg(_errors)
                    .arg(_warnings);
    }
    if (textState && Member(*textState, "typing") && Member(*textState, "typing")->b)
        line += tr("  ·  a line being typed (not in the text until Enter)");
    if (cursor >= 0)
        line += tr("  ·  cursor at line %1").arg(cursor + 1);
    const std::string state = Text(status, "state");
    if (state == "ambiguous")
        line += tr("  ·  two assemblers identify alike");
    if (!Text(status, "error").empty())
        line += "  ·  " + QString::fromStdString(Text(status, "error"));
    _status->setText(line);
    const bool haveText = generation > 0;
    _extract->setEnabled(haveText);
    _convert->setEnabled(haveText);
}

void LiveSourceWindow::highlight(int cursorLine)
{
    QList<QTextEdit::ExtraSelection> selections;
    for (const auto& [line, severity] : _hintLines)
    {
        const QTextBlock block = _text->document()->findBlockByNumber(line - 1);
        if (!block.isValid())
            continue;
        QTextEdit::ExtraSelection s;
        s.cursor = QTextCursor(block);
        s.format.setProperty(QTextFormat::FullWidthSelection, true);
        s.format.setBackground(severity == "error" ? QColor(0xFF, 0xD8, 0xD8) : QColor(0xFF, 0xF2, 0xC8));
        selections.append(s);
    }
    if (cursorLine >= 0)
    {
        const QTextBlock block = _text->document()->findBlockByNumber(cursorLine);
        if (block.isValid())
        {
            QTextEdit::ExtraSelection s;
            s.cursor = QTextCursor(block);
            s.format.setProperty(QTextFormat::FullWidthSelection, true);
            s.format.setBackground(QColor(0xD8, 0xE8, 0xFF));
            selections.append(s);
        }
    }
    _text->setExtraSelections(selections);
}

void LiveSourceWindow::goToHint()
{
    const QListWidgetItem* item = _hints->currentItem();
    if (!item)
        return;
    const int line = item->data(Qt::UserRole).toInt();
    const QTextBlock block = _text->document()->findBlockByNumber(line - 1);
    if (!block.isValid())
        return;
    _text->setTextCursor(QTextCursor(block));
    _text->centerCursor();
}

void LiveSourceWindow::extract()
{
    const std::shared_ptr<Emulator> emulator = instance();
    if (!emulator)
        return;
    const AsmReply status = AsmControl(emulator->GetContext()).Execute({"sync-status", {}});
    if (!status.Ok())
    {
        QMessageBox::warning(this, tr("Extract"), QString::fromStdString(status.message));
        return;
    }
    const QString name = QString::fromStdString(Text(status.body, "name")).isEmpty() ? "live" : QString::fromStdString(Text(status.body, "name"));
    const QString type = Text(status.body, "assembler").rfind("tasm", 0) == 0 ? "A" : "H";
    const QString output = QFileDialog::getSaveFileName(this, tr("Extract the Source"), name + ".txt",
                                                        tr("Text (*.txt *.asm);;The assembler's file (*.%1 *.$%1);;All Files (*)").arg(type));
    if (output.isEmpty())
        return;
    const QString suffix = QFileInfo(output).suffix().toUpper();
    const bool asFile = suffix == type || suffix == "$" + type;
    const AsmReply reply = AsmControl(emulator->GetContext())
                               .Execute({"sync-extract", {{"as", asFile ? "file" : "text"}, {"output", output.toStdString()}}});
    if (!reply.Ok())
        QMessageBox::warning(this, tr("Extract"), QString::fromStdString(reply.message));
}

void LiveSourceWindow::convert()
{
    const std::shared_ptr<Emulator> emulator = instance();
    if (!emulator)
        return;
    const AsmReply list = AsmControl(nullptr).Execute({"dialects", {}});
    QStringList dialects;
    if (const StateNode* write = list.body.find("write"))
        for (const StateNode& d : write->items)
            dialects << QString::fromStdString(d.s);
    bool ok = false;
    const QString to = QInputDialog::getItem(this, tr("Convert to"), tr("Dialect:"), dialects,
                                             std::max<int>(0, static_cast<int>(dialects.indexOf("sjasmplus"))), false, &ok);
    if (!ok || to.isEmpty())
        return;
    const QString output = QFileDialog::getSaveFileName(this, tr("Save the Conversion"), "live.asm", tr("Assembler source (*.asm *.s *.z80);;All Files (*)"));
    if (output.isEmpty())
        return;
    const AsmReply reply = AsmControl(emulator->GetContext())
                               .Execute({"sync-extract", {{"as", "dialect"}, {"to", to.toStdString()}, {"output", output.toStdString()}}});
    if (!reply.Ok())
        QMessageBox::warning(this, tr("Convert"), QString::fromStdString(reply.message));
}
