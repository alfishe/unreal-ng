#ifndef DISASSEMBLERWIDGET_H
#define DISASSEMBLERWIDGET_H

#include <QObject>
#include <QWidget>
#include <QPlainTextEdit>

#include "emulator/emulatorcontext.h"

QT_BEGIN_NAMESPACE
namespace Ui { class DisassemblerWidget; }
QT_END_NAMESPACE

class DisassemblerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DisassemblerWidget(QWidget *parent = nullptr);
    virtual ~DisassemblerWidget() override;

    void init(EmulatorContext* context);
    void detach();

    void setDisassemblerAddress(uint16_t pc);

public slots:
    void reset();
    void refresh();

signals:

private:
    Ui::DisassemblerWidget* ui;
    QThread* m_mainThread;
    QPlainTextEdit* m_disassemblyTextEdit;

    EmulatorContext* m_emulatorContext;
};

#endif // DISASSEMBLERWIDGET_H
