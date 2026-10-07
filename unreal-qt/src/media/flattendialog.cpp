#include "flattendialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include <utility>

namespace
{
    QString Q(const std::string& text) { return QString::fromStdString(text); }

    bool ReplyOk(const StateNode& reply)
    {
        const StateNode* ok = reply.find("ok");
        return ok && ok->kind == StateNode::Kind::Bool && ok->b;
    }

    QString ReplyText(const StateNode& reply)
    {
        QStringList lines;
        if (!ReplyOk(reply))
            if (const StateNode* message = reply.find("message"); message && message->kind == StateNode::Kind::String)
                lines << Q(message->s);
        for (const std::string& line : ReplyReport(reply))
            lines << Q(line);
        return lines.join("\n");
    }

    /// `media changes` as lines: "create /GAMES/NEW.TRD [games]"
    QString ChangesText(const StateNode& reply)
    {
        QStringList lines;
        if (const StateNode* changes = reply.find("changes"))
        {
            for (const StateNode& c : changes->items)
            {
                QString line = Q(c.find("op") ? c.find("op")->s : "") + " ";
                if (const StateNode* old = c.find("oldPath"))
                    line += Q(old->s) + " -> ";
                line += Q(c.find("path") ? c.find("path")->s : "");
                if (const StateNode* layer = c.find("layer"); layer && !layer->s.empty())
                    line += " [" + Q(layer->s) + "]";
                lines << line;
            }
        }
        if (const StateNode* warnings = reply.find("warnings"))
            for (const StateNode& w : warnings->items)
                lines << QObject::tr("warning: %1").arg(Q(w.s));
        return lines.isEmpty() ? QObject::tr("(no file changes found)") : lines.join("\n");
    }
}  // namespace

FlattenDialog::FlattenDialog(const std::string& slot, Runner runner, const QString& lastDirectory, QWidget* parent)
    : QDialog(parent), _slot(slot), _run(std::move(runner)), _lastDirectory(lastDirectory)
{
    setWindowTitle(tr("Save the changes of %1").arg(Q(slot)));
    auto* layout = new QVBoxLayout(this);

    const StateNode layers = _run("layers", "", {});
    const StateNode changes = _run("changes", "", {});
    _options = FlattenOptionsFor(layers);
    const FlattenStrategy preselected = PreselectedStrategy(_options, layers);

    _text = new QPlainTextEdit(this);
    _text->setReadOnly(true);
    _text->setPlainText(ChangesText(changes));
    _text->setMinimumSize(560, 160);

    auto* group = new QButtonGroup(this);
    for (const FlattenOption& o : _options)
    {
        auto* radio = new QRadioButton(Q(o.title), this);
        radio->setToolTip(Q(o.help));
        radio->setEnabled(o.available);
        radio->setChecked(o.strategy == preselected);
        group->addButton(radio);
        layout->addWidget(radio);
        auto* help = new QLabel(Q(o.available ? o.help : o.help + " - " + o.reason), this);
        help->setWordWrap(true);
        help->setIndent(24);
        help->setEnabled(o.available);
        layout->addWidget(help);
        if (o.strategy == FlattenStrategy::Flat)
        {
            auto* row = new QHBoxLayout();
            row->addSpacing(24);
            _path = new QLineEdit(this);
            _path->setPlaceholderText(tr("image file (.img, .vhd, .chd)"));
            _browse = new QPushButton(tr("Browse..."), this);
            _compact = new QCheckBox(tr("Compact (every file in one piece)"), this);
            row->addWidget(_path, 1);
            row->addWidget(_browse);
            row->addWidget(_compact);
            layout->addLayout(row);
            connect(_browse, &QPushButton::clicked, this, [this] {
                const QString target = QFileDialog::getSaveFileName(this, tr("Save as one image"), _lastDirectory,
                                                                    tr("Disk images (*.img *.vhd *.chd);;All files (*)"));
                if (!target.isEmpty())
                    _path->setText(target);
            });
        }
        if (o.strategy == FlattenStrategy::WriteBack)
        {
            auto* row = new QHBoxLayout();
            row->addSpacing(24);
            _keepBoth = new QCheckBox(tr("When a host file changed meanwhile, keep both (\"name (guest).ext\")"), this);
            row->addWidget(_keepBoth);
            layout->addLayout(row);
        }
        _radios.push_back(radio);
        connect(radio, &QRadioButton::toggled, this, [this] { UpdateControls(); });
    }
    _force = new QCheckBox(tr("Even if the guest's file system has lost clusters (commit, write-back)"), this);
    layout->addWidget(_force);
    layout->addWidget(new QLabel(tr("The guest's changes (Preview shows what a commit or write-back writes):"), this));
    layout->addWidget(_text);

    auto* buttons = new QDialogButtonBox(this);
    _preview = buttons->addButton(tr("Preview"), QDialogButtonBox::ActionRole);
    QPushButton* save = buttons->addButton(tr("Save"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(_preview, &QPushButton::clicked, this, [this] { Preview(); });
    connect(save, &QPushButton::clicked, this, [this] { RunChosen(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    UpdateControls();
}

FlattenChoice FlattenDialog::Choice() const
{
    FlattenChoice choice;
    for (size_t i = 0; i < _radios.size(); i++)
        if (_radios[i]->isChecked())
            choice.strategy = _options[i].strategy;
    choice.path = _path ? _path->text().trimmed().toStdString() : std::string();
    choice.compact = _compact && _compact->isChecked();
    choice.keepBoth = _keepBoth && _keepBoth->isChecked();
    choice.force = _force && _force->isChecked();
    return choice;
}

void FlattenDialog::UpdateControls()
{
    if (!_path || !_keepBoth || !_force || !_preview)
        return;  // a radio toggled while the dialog is still being built
    const FlattenStrategy s = Choice().strategy;
    const bool flat = s == FlattenStrategy::Flat;
    _path->setEnabled(flat);
    _browse->setEnabled(flat);
    _compact->setEnabled(flat);
    _keepBoth->setEnabled(s == FlattenStrategy::WriteBack);
    _force->setEnabled(s == FlattenStrategy::Commit || s == FlattenStrategy::WriteBack);
    _preview->setEnabled(s == FlattenStrategy::Commit || s == FlattenStrategy::WriteBack);
}

void FlattenDialog::Preview()
{
    const FlattenChoice choice = Choice();
    const StateNode reply = _run("flatten", "", FlattenRequestOptions(choice, /*plan*/ true));
    _text->setPlainText(ReplyText(reply));
}

void FlattenDialog::RunChosen()
{
    const FlattenChoice choice = Choice();
    if (choice.strategy == FlattenStrategy::Flat && choice.path.empty())
    {
        QMessageBox::information(this, windowTitle(), tr("Name the image file to write."));
        return;
    }
    const StateNode reply = _run("flatten", choice.strategy == FlattenStrategy::Flat ? choice.path : "",
                                 FlattenRequestOptions(choice, /*plan*/ false));
    if (!ReplyOk(reply))
    {
        // Stay open: a conflict or a refusal is shown, the user picks again
        _text->setPlainText(ReplyText(reply));
        QMessageBox::warning(this, windowTitle(), ReplyText(reply).section('\n', 0, 0));
        return;
    }
    _result = reply;
    accept();
}
