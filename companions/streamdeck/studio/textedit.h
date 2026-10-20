// Edit commands (copy / cut / paste / delete / select all) for the text field that has the focus. The scenario
// editor binds the same keys to nodes; when a line edit, the JSON box or a log has the focus the keys are its own.
#pragma once
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QKeySequence>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>

// The focused text widget's handling of `k`; false when the focus is not in text (the caller acts on nodes then).
inline bool TextCommand(QKeySequence::StandardKey k, QWidget* w = QApplication::focusWidget())
{
    if (auto* sb = qobject_cast<QAbstractSpinBox*>(w))
        w = sb->findChild<QLineEdit*>();
    if (auto* cb = qobject_cast<QComboBox*>(w); cb && cb->isEditable())
        w = cb->lineEdit();
    if (auto* e = qobject_cast<QLineEdit*>(w))
    {
        switch (k)
        {
            case QKeySequence::Copy:
                e->copy();
                break;
            case QKeySequence::Cut:
                if (!e->isReadOnly())
                    e->cut();
                break;
            case QKeySequence::Paste:
                if (!e->isReadOnly())
                    e->paste();
                break;
            case QKeySequence::Delete:
                if (!e->isReadOnly())
                    e->del();
                break;
            case QKeySequence::SelectAll:
                e->selectAll();
                break;
            default:
                return false;
        }
        return true;
    }
    auto run = [k](auto* e) {
        switch (k)
        {
            case QKeySequence::Copy:
                e->copy();
                break;
            case QKeySequence::Cut:
                if (!e->isReadOnly())
                    e->cut();
                break;
            case QKeySequence::Paste:
                if (!e->isReadOnly())
                    e->paste();
                break;
            case QKeySequence::Delete:
                if (!e->isReadOnly())
                    e->textCursor().deleteChar();
                break;
            case QKeySequence::SelectAll:
                e->selectAll();
                break;
            default:
                return false;
        }
        return true;
    };
    if (auto* e = qobject_cast<QPlainTextEdit*>(w))
        return run(e);
    if (auto* e = qobject_cast<QTextEdit*>(w))
        return run(e);
    return false;
}

// A read-only log: selectable with the mouse and the keyboard, so Cmd+C / Cmd+A reach it.
inline void MakeSelectableLog(QPlainTextEdit* e)
{
    e->setReadOnly(true);
    e->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
}
