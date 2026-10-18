#ifndef BUILDOUTPUT_H
#define BUILDOUTPUT_H

#include <QPlainTextEdit>

class BuildOutput : public QPlainTextEdit
{
    Q_OBJECT

public:
    enum MessageType {
        Info,
        Success,
        Warning,
        Error,
        Command
    };
    
    explicit BuildOutput(QWidget *parent = nullptr);
    
    void appendLine(const QString &text, MessageType type = Info);
    void clear();

signals:
    void lineAppended(const QString &text, MessageType type);

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void setupFormat();
};

#endif // BUILDOUTPUT_H