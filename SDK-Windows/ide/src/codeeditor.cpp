#include "codeeditor.h"
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QPainter>
#include <QScrollBar>

CodeEditor::CodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
    , m_lineNumberArea(new QWidget(this))
    , m_highlighter(new Highlighter(document()))
{
    setFont(QFont("Consolas", 10));
    setTabStopDistance(QFontMetrics(font()).horizontalAdvance(' ') * 4);
    
    connect(document(), &QTextDocument::contentsChanged, this, [this](){
        m_hasUnsavedChanges = true;
    });
    
    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);
    
    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
    
    setLineWrapMode(QPlainTextEdit::NoWrap);
}

bool CodeEditor::openFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    setPlainText(stream.readAll());
    file.close();
    
    m_filePath = path;
    setFilePath(path);
    document()->setModified(false);
    return true;
}

bool CodeEditor::save()
{
    if (m_filePath.isEmpty()) {
        return false;
    }
    return saveAs(m_filePath);
}

bool CodeEditor::saveAs(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << toPlainText();
    file.close();
    
    m_filePath = path;
    setFilePath(path);
    document()->setModified(false);
    return true;
}

QString CodeEditor::filePath() const
{
    return m_filePath;
}

QString CodeEditor::fileName() const
{
    return QFileInfo(m_filePath).fileName();
}

void CodeEditor::setFilePath(const QString &path)
{
    m_filePath = path;
}

bool CodeEditor::isModified() const
{
    return document()->isModified();
}

int CodeEditor::lineNumberAreaWidth()
{
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }
    int space = 3 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    return space;
}

void CodeEditor::updateLineNumberAreaWidth(int)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy) {
        m_lineNumberArea->scroll(0, dy);
    } else {
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) {
        updateLineNumberAreaWidth(0);
    }
}

void CodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> extraSelections;
    
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        QColor lineColor = QColor(60, 60, 65);
        selection.format.setBackground(lineColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }
    
    setExtraSelections(extraSelections);
}

void CodeEditor::paintEvent(QPaintEvent *event)
{
    QPlainTextEdit::paintEvent(event);
}

Highlighter::Highlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(QColor(86, 156, 214)); // Blue
    keywordFormat.setFontWeight(QFont::Bold);
    
    QStringList keywordPatterns = {
        "\\bchar\\b", "\\bclass\\b", "\\bconst\\b", "\\bdouble\\b", "\\benum\\b",
        "\\bexplicit\\b", "\\bfriend\\b", "\\binline\\b", "\\bint\\b", "\\blong\\b",
        "\\bnamespace\\b", "\\boperator\\b", "\\bprivate\\b", "\\bprotected\\b",
        "\\bpublic\\b", "\\bshort\\b", "\\bsignals\\b", "\\bsigned\\b", "\\bslots\\b",
        "\\bstatic\\b", "\\bstruct\\b", "\\btemplate\\b", "\\btypedef\\b",
        "\\btypename\\b", "\\bunion\\b", "\\bunsigned\\b", "\\bvirtual\\b",
        "\\bvoid\\b", "\\bvolatile\\b", "\\bbool\\b", "\\btrue\\b", "\\bfalse\\b",
        "\\bif\\b", "\\belse\\b", "\\bwhile\\b", "\\bfor\\b", "\\bdo\\b",
        "\\bswitch\\b", "\\bcase\\b", "\\bdefault\\b", "\\bbreak\\b", "\\bcontinue\\b",
        "\\breturn\\b", "\\bgoto\\b", "\\bsizeof\\b", "\\bnew\\b", "\\bdelete\\b",
        "\\btry\\b", "\\bcatch\\b", "\\bthrow\\b", "\\bextern\\b", "\\bregister\\b",
        "\\bauto\\b", "\\bconstexpr\\b", "\\bnullptr\\b", "\\bstatic_assert\\b",
        "\\bthread_local\\b", "\\balignas\\b", "\\balignof\\b"
    };
    
    for (const QString &pattern : keywordPatterns) {
        HighlightingRule rule;
        rule.pattern = QRegularExpression(pattern);
        rule.format = keywordFormat;
        m_rules.append(rule);
    }
    
    // Kresten-specific keywords
    QTextCharFormat krestenFormat;
    krestenFormat.setForeground(QColor(198, 120, 221)); // Purple
    krestenFormat.setFontWeight(QFont::Bold);
    
    QStringList krestenPatterns = {
        "\\bPLUGIN_DEFINE\\b", "\\bREGISTER_PLUGIN\\b", "\\bREGISTER_DRIVER\\b",
        "\\bEXPORT_SYMBOL\\b", "\\bDriver\\b", "\\bPlugin\\b", "\\bkplg_plugin\\b",
        "\\bkdrv_driver\\b", "\\bvm_guest\\b", "\\blx_guest\\b"
    };
    
    for (const QString &pattern : krestenPatterns) {
        HighlightingRule rule;
        rule.pattern = QRegularExpression(pattern);
        rule.format = krestenFormat;
        m_rules.append(rule);
    }
    
    // Types
    QTextCharFormat typeFormat;
    typeFormat.setForeground(QColor(78, 201, 176)); // Teal
    QStringList typePatterns = {
        "\\buint8_t\\b", "\\buint16_t\\b", "\\buint32_t\\b", "\\buint64_t\\b",
        "\\bint8_t\\b", "\\bint16_t\\b", "\\bint32_t\\b", "\\bint64_t\\b",
        "\\bsize_t\\b", "\\bptrdiff_t\\b", "\\bvoid\\*\\b"
    };
    for (const QString &pattern : typePatterns) {
        HighlightingRule rule;
        rule.pattern = QRegularExpression(pattern);
        rule.format = typeFormat;
        m_rules.append(rule);
    }
    
    // Strings
    QTextCharFormat stringFormat;
    stringFormat.setForeground(QColor(214, 157, 133)); // Orange
    HighlightingRule rule;
    rule.pattern = QRegularExpression("\".*\"");
    rule.format = stringFormat;
    m_rules.append(rule);
    
    rule.pattern = QRegularExpression("'.*'");
    rule.format = stringFormat;
    m_rules.append(rule);
    
    // Numbers
    QTextCharFormat numberFormat;
    numberFormat.setForeground(QColor(181, 206, 168)); // Light green
    rule.pattern = QRegularExpression("\\b\\d+\\b");
    rule.format = numberFormat;
    m_rules.append(rule);
    
    // Preprocessor
    QTextCharFormat preprocessorFormat;
    preprocessorFormat.setForeground(QColor(224, 108, 117)); // Red
    preprocessorFormat.setFontItalic(true);
    rule.pattern = QRegularExpression("^\\s*#.*");
    rule.format = preprocessorFormat;
    m_rules.append(rule);
    
    // Comments
    QTextCharFormat commentFormat;
    commentFormat.setForeground(QColor(106, 153, 85)); // Green
    rule.pattern = QRegularExpression("//.*");
    rule.format = commentFormat;
    m_rules.append(rule);
    
    m_commentStartExpression = QRegularExpression("/\\*");
    m_commentEndExpression = QRegularExpression("\\*/");
    m_multiLineCommentFormat.setForeground(QColor(106, 153, 85));
}

void Highlighter::highlightBlock(const QString &text)
{
    // Single-line rules
    for (const HighlightingRule &rule : m_rules) {
        QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
    
    // Multi-line comments
    setCurrentBlockState(0);
    int startIndex = 0;
    
    if (previousBlockState() != 1) {
        startIndex = text.indexOf(m_commentStartExpression);
    }
    
    while (startIndex >= 0) {
        QRegularExpressionMatch match = m_commentEndExpression.match(text, startIndex);
        int endIndex = match.capturedStart();
        int commentLength = 0;
        
        if (endIndex == -1) {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        } else {
            commentLength = endIndex - startIndex + match.capturedLength();
        }
        
        setFormat(startIndex, commentLength, m_multiLineCommentFormat);
        startIndex = text.indexOf(m_commentStartExpression, startIndex + commentLength);
    }
}