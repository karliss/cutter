#include "TerminalDisplay.h"

#include "core/Cutter.h"

#include <QVBoxLayout>
#include <QScrollBar>

TerminalDisplay::TerminalDisplay(QWidget *parent) : TerminalDisplayBase { parent }
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    this->setContentsMargins(0, 0, 0, 0);
    auto layout = new QVBoxLayout(this);
    setLayout(layout);
    outputTextEdit = new QPlainTextEdit(this);
    layout->addWidget(outputTextEdit);
    outputTextEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    outputTextEdit->setReadOnly(true);
    outputTextEdit->setContextMenuPolicy(Qt::NoContextMenu);

    QTextDocument *console_docu = outputTextEdit->document();
    console_docu->setDocumentMargin(10);
}

void TerminalDisplay::AddOutput(const QString &input)
{
    QString output = input;
    // Get the last segment that wasn't overwritten by carriage return
    output = output.trimmed();
    output = output.remove(0, output.lastIndexOf('\r')).trimmed();

    outputTextEdit->appendHtml(CutterCore::ansiEscapeToHtml(input));
}

void TerminalDisplay::clear()
{
    outputTextEdit->clear();
}

void TerminalDisplay::setWrap(bool wrap)
{
    outputTextEdit->setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}

bool TerminalDisplay::wrap()
{
    return outputTextEdit->lineWrapMode() != QPlainTextEdit::NoWrap;
}

void TerminalDisplay::setFont(const QFont &font)
{
    outputTextEdit->setFont(font);
}

void TerminalDisplay::scrollToEnd()
{
    const int maxValue = outputTextEdit->verticalScrollBar()->maximum();
    outputTextEdit->verticalScrollBar()->setValue(maxValue);
}

QPlainTextEdit *TerminalDisplay::TextWidget()
{
    return outputTextEdit;
}
