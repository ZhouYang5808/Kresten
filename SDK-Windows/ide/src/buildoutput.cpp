#include "buildoutput.h"
#include <QTextCharFormat>
#include <QColor>
#include <QTextCursor>
#include <QRegularExpression>
#include <QMenu>
#include <QAction>

BuildOutput::BuildOutput(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setFont(QFont("Consolas", 9));
    setMaximumBlockCount(10000);
    setupFormat();
    
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        QMenu menu(this);
        menu.addAction("Clear", this, &BuildOutput::clear);
        menu.addAction("Copy", this, &BuildOutput::copy);
        menu.addAction("Select All", this, &BuildOutput::selectAll);
        menu.exec(mapToGlobal(pos));
    });
}

void BuildOutput::setupFormat()
{
    // Style for different message types will be applied per line
}

void BuildOutput::appendLine(const QString &text, MessageType type)
{
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);
    setTextCursor(cursor);
    
    QTextCharFormat format;
    switch (type) {
        case Info:
            format.setForeground(QColor(200, 200, 200));
            break;
        case Success:
            format.setForeground(QColor(100, 220, 100));
            format.setFontWeight(QFont::Bold);
            break;
        case Warning:
            format.setForeground(QColor(255, 200, 50));
            format.setFontWeight(QFont::Bold);
            break;
        case Error:
            format.setForeground(QColor(255, 100, 100));
            format.setFontWeight(QFont::Bold);
            break;
        case Command:
            format.setForeground(QColor(150, 150, 255));
            format.setFontItalic(true);
            break;
    }
    
    cursor.insertText(text + "\n", format);
    ensureCursorVisible();
}

void BuildOutput::clear()
{
    QPlainTextEdit::clear();
}

void BuildOutput::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Parse error lines for file:line references
    QTextCursor cursor = cursorForPosition(event->pos());
    cursor.select(QTextCursor::LineUnderCursor);
    QString line = cursor.selectedText();
    
    // Match patterns like "file.c:123:" or "file.c:123:45:"
    QRegularExpression re(R"(([\w/\.-]+\.c):(\d+):?)");
    QRegularExpressionMatch match = re.match(line);
    if (match.hasMatch()) {
        QString file = match.captured(1);
        int lineNum = match.captured(2).toInt();
        emit lineAppended(file + ":" + QString::number(lineNum), Info);
    }
    
    QPlainTextEdit::mouseDoubleClickEvent(event);
}