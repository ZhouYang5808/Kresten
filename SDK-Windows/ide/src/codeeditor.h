#ifndef CODEEDITOR_H
#define CODEEDITOR_H

#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QPainter>
#include <QTextBlock>

class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit CodeEditor(QWidget *parent = nullptr);
    
    bool openFile(const QString &path);
    bool save();
    bool saveAs(const QString &path);
    QString filePath() const;
    QString fileName() const;
    void setFilePath(const QString &path);
    
    void highlightCurrentLine();
    void setupHighlighter();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QString m_filePath;
    QWidget *m_lineNumberArea;
    class Highlighter *m_highlighter;
    
    int lineNumberAreaWidth();
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect &rect, int dy);
    void highlightCurrentLine();
    
    friend class Highlighter;
};

class Highlighter : public QSyntaxHighlighter
{
public:
    Highlighter(QTextDocument *parent = nullptr);
    
protected:
    void highlightBlock(const QString &text) override;

private:
    struct HighlightingRule
    {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<HighlightingRule> m_rules;
    
    QRegularExpression m_commentStartExpression;
    QRegularExpression m_commentEndExpression;
    QTextCharFormat m_multiLineCommentFormat;
};

#endif // CODEEDITOR_H