/**
 * @file tapeblockdialog.cpp
 * @brief Implementation of TapeBlockDialog (r7 block-content popup).
 */

#include "tapeblockdialog.h"

#include <QColor>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QLabel>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

#include "QHexView/model/buffer/qmemorybuffer.h"
#include "QHexView/qhexview.h"

#include "debugger/analyzers/basic-lang/basicextractor.h"

namespace
{
QString SecondsText(double seconds)
{
    return QString::number(seconds, 'f', 1) + QStringLiteral("s");
}

/// True when a data block's paired header declares a BASIC program body.
bool IsBasicProgramBlock(const TapeBlockDescriptor& descriptor, const TapeBlockDescriptor* pairedHeader)
{
    return descriptor.kind == TapeBlockKindEnum::Data && pairedHeader != nullptr &&
           pairedHeader->headerValid && pairedHeader->headerType == TAP_BLOCK_PROGRAM;
}
}  // namespace

TapeBlockDialog::TapeBlockDialog(const TapeBlockDescriptor& descriptor,
                                 const TapeBlockDescriptor* pairedHeader,
                                 const std::vector<uint8_t>& payload,
                                 QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("TapeBlockDialog"));

    // ---- Title: "Block 3 — Data 'parallax'" (+ pair context for bodies — r8) ----
    QString title = tr("Block %1 — %2")
                        .arg(static_cast<qulonglong>(descriptor.index))
                        .arg(QString::fromUtf8(getTapeBlockKindName(descriptor.kind)));
    if (!descriptor.name.empty())
    {
        title += tr(" '%1'").arg(QString::fromStdString(descriptor.name));
    }
    if (pairedHeader != nullptr && pairedHeader->headerValid)
    {
        QString pairText = tr("body of %1").arg(QString::fromUtf8(getTapeBlockTypeName(pairedHeader->headerType)));
        if (!pairedHeader->name.empty())
        {
            pairText += tr(" '%1'").arg(QString::fromStdString(pairedHeader->name));
        }
        title += tr(" · %1").arg(pairText);
    }
    setWindowTitle(title);

    // ---- Summary line: size, duration, checksum (+ header interpretation) ----
    QString summary = tr("%1 bytes · ~%2").arg(payload.size()).arg(SecondsText(descriptor.estimatedSeconds));
    if (!payload.empty())
    {
        summary += tr(" · checksum %1").arg(descriptor.checksumValid ? QStringLiteral("OK") : QStringLiteral("INVALID"));
    }
    if (descriptor.headerValid)
    {
        summary += tr("<br>%1 '%2', length %3, params %4/%5")
                       .arg(QString::fromUtf8(getTapeBlockTypeName(descriptor.headerType)))
                       .arg(QString::fromStdString(descriptor.name).toHtmlEscaped())
                       .arg(descriptor.declaredLength)
                       .arg(descriptor.param1)
                       .arg(descriptor.param2);
    }

    auto* layout = new QVBoxLayout(this);
    auto* summaryLabel = new QLabel(summary, this);
    summaryLabel->setTextFormat(Qt::RichText);
    summaryLabel->setWordWrap(true);
    layout->addWidget(summaryLabel);

    // ---- Content: BASIC listing, hex dump, or no-payload note ----
    bool decoded = false;
    if (IsBasicProgramBlock(descriptor, pairedHeader) && descriptor.flagBytePresent && payload.size() >= 3)
    {
        // Body sits between the $FF flag and the trailing checksum; the
        // extractor walks the line headers (lineNo u16 BE + lineLen u16 LE)
        std::vector<uint8_t> body(payload.begin() + 1, payload.end() - 1);
        BasicExtractor extractor;
        const BasicListing listing = extractor.extractBasicLines(body.data(), body.size());
        if (!listing.lines.empty())
        {
            // r9 readable formatting: compacted single-line programs (a huge
            // line-0 REM stuffed with machine code) wrap, but a grey
            // right-aligned line-number gutter plus a hanging indent keeps a
            // new logical line distinguishable from a wrapped continuation,
            // and the variables tail a SAVE'd program carries renders as an
            // explicit note instead of a bogus "60928 ..." pseudo-line
            auto* basicView = new QTextEdit(this);
            basicView->setReadOnly(true);
            const QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
            basicView->setFont(monoFont);

            const int gutterWidth = QFontMetrics(monoFont).horizontalAdvance(QStringLiteral("   0  "));
            QTextBlockFormat hangingFormat;
            hangingFormat.setLeftMargin(gutterWidth);
            hangingFormat.setTextIndent(-gutterWidth);  // first line reaches into the gutter
            hangingFormat.setTopMargin(0);
            hangingFormat.setBottomMargin(0);

            QTextCharFormat numberFormat;
            numberFormat.setForeground(QColor(0x90, 0x90, 0x90));
            numberFormat.setFont(monoFont);

            QTextCharFormat textFormat;
            textFormat.setFont(monoFont);

            QTextCursor cursor(basicView->document());
            bool firstBlock = true;
            for (const BasicLine& line : listing.lines)
            {
                if (!firstBlock)
                {
                    cursor.insertBlock();
                }
                firstBlock = false;

                if (line.variablesArea)
                {
                    QTextBlockFormat noteFormat;
                    noteFormat.setTopMargin(8);
                    noteFormat.setBottomMargin(0);
                    cursor.setBlockFormat(noteFormat);
                    QTextCharFormat noteFormatChar;
                    noteFormatChar.setForeground(QColor(0x70, 0x70, 0x70));
                    noteFormatChar.setFontItalic(true);
                    noteFormatChar.setFont(monoFont);
                    cursor.setCharFormat(noteFormatChar);
                    cursor.insertText(tr("— variables area: %1 bytes after the listing —").arg(listing.variablesBytes));
                    break;  // everything past the vars marker is data, not program
                }

                cursor.setBlockFormat(hangingFormat);
                cursor.setCharFormat(numberFormat);
                cursor.insertText(QString::number(line.lineNumber).rightJustified(4));
                cursor.setCharFormat(textFormat);
                cursor.insertText(QStringLiteral("  ") + QString::fromStdString(line.text));
            }
            layout->addWidget(basicView, 1);
            decoded = true;
        }
        // An empty decode falls through to the hex dump — honest fallback
    }

    if (!decoded)
    {
        if (payload.empty())
        {
            auto* info = new QLabel(tr("Pulse stream / control entry — this block carries no byte payload."), this);
            info->setWordWrap(true);
            layout->addWidget(info, 0, Qt::AlignTop);
        }
        else
        {
            auto* hexView = new QHexView(this);
            QByteArray bytes(reinterpret_cast<const char*>(payload.data()), static_cast<int>(payload.size()));
            QHexDocument* document = QHexDocument::fromMemory<QMemoryBuffer>(bytes);
            hexView->setDocument(document);
            QHexOptions options = hexView->options();
            options.linelength = 8;      // same 8-bytes-per-line as the debugger views
            options.addresswidth = 4;
            hexView->setOptions(options);
            layout->addWidget(hexView, 1);
        }
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    resize(560, 420);
}
