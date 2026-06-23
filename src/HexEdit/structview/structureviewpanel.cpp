#include "structview/structureviewpanel.h"

#include "HexView/hexview.h"
#include "combos/menucombobox.h"
#include "filestats/banner.h"
#include "filestats/widgets.h"
#include "structview/structuredefinitionmanager.h"
#include "structview/structuregriditemdelegate.h"
#include "structview/structuretreemodel.h"
#include "structview/structurevaluebuilder.h"
#include "theme.h"

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QTextEdit>
#include <QDebug>
#include <QElapsedTimer>
#include <QEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOptionHeader>
#include <QStackedWidget>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextStream>
#include <QTextTable>
#include <QTimer>
#include <QTreeView>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace
{
static constexpr int kHeaderBottomGap = 3;

enum class InitialStructureExpansion
{
    Collapsed,
    FirstLevel,
    FirstLevelAndFirstField,
    All,
};

static constexpr InitialStructureExpansion kInitialStructureExpansion =
    InitialStructureExpansion::FirstLevelAndFirstField;
static constexpr int kBranchIconSize = 16;

// m_rootCombo item data roles used to mark an entry as a definition file that
// failed to parse: kRootComboFilePathRole holds the source path (empty for a
// normal, successfully-parsed type), kRootComboErrorRole the parser diagnostic.
static constexpr int kRootComboFilePathRole = Qt::UserRole + 1;
static constexpr int kRootComboErrorRole    = Qt::UserRole + 2;

static bool structureProfileEnabled()
{
    return qEnvironmentVariableIntValue("QEXED_STRUCTURE_PROFILE") != 0;
}

static void structureProfileLog(const QString &message)
{
    qInfo().noquote() << message;

    const QString path = qEnvironmentVariable("QEXED_STRUCTURE_PROFILE_LOG",
                                              QStringLiteral("structure-profile.log"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    QTextStream stream(&file);
    stream << message << Qt::endl;
}

static size_t structureRowCount(const StructureRow *row)
{
    if (!row)
        return 0;

    size_t count = 1;
    for (const auto &child : row->children)
        count += structureRowCount(child.get());
    return count;
}

static size_t structureRowCount(const std::vector<std::unique_ptr<StructureRow>> &rows)
{
    size_t count = 0;
    for (const auto &row : rows)
        count += structureRowCount(row.get());
    return count;
}

// DFS walk of eagerly-loaded rows (no lazy-load trigger) to find the first code target.
static bool findFirstCodeTarget(StructureTreeModel *model, const QModelIndex &parent, uint64_t *result)
{
    const int n = model->rowCount(parent);
    for (int r = 0; r < n; ++r)
    {
        const QModelIndex idx = model->index(r, 0, parent);
        if (const StructureRow *row = model->rowForIndex(idx))
        {
            if (row->hasCodeTarget)
            {
                *result = row->codeTargetOffset;
                return true;
            }
        }
        if (findFirstCodeTarget(model, idx, result))
            return true;
    }
    return false;
}

static QFont structureSourceViewFont(const QFont &hexViewFont)
{
    QFont font = hexViewFont;
    const QFont defaultFont = QApplication::font();

    if (font.pointSizeF() > 0)
    {
        const qreal defaultSize = defaultFont.pointSizeF() > 0 ? defaultFont.pointSizeF() : font.pointSizeF();
        font.setPointSizeF(qMax(defaultSize, font.pointSizeF() - 2.0));
    }
    else if (font.pixelSize() > 0)
    {
        const int defaultSize = defaultFont.pixelSize() > 0 ? defaultFont.pixelSize() : QFontMetrics(defaultFont).height();
        font.setPixelSize(qMax(defaultSize, font.pixelSize() - 2));
    }

    return font;
}

static void applyStructureTextViewPalette(QPlainTextEdit *edit, HexView *hv)
{
    if (!edit)
        return;

    const QColor bgColor = hv ? QColor(hv->getHexColour(HVC_BACKGROUND))
                              : edit->palette().color(QPalette::Base);
    const QColor selBgColor = hv ? QColor(hv->getHexColour(HVC_SELECTION))
                                 : edit->palette().color(QPalette::Highlight);
    const QColor selFgColor = hv ? QColor(hv->getHexColour(HVC_SELTEXT))
                                 : edit->palette().color(QPalette::HighlightedText);

    QPalette editPalette = edit->palette();
    editPalette.setColor(QPalette::Base, bgColor);
    edit->setPalette(editPalette);
    edit->setFrameShape(QFrame::NoFrame);
    edit->setStyleSheet(
        QStringLiteral("QPlainTextEdit#%1 { border: none; padding: 0;"
                       " selection-background-color: %2; selection-color: %3; }")
            .arg(edit->objectName(),
                 filestats::cssColor(selBgColor),
                 filestats::cssColor(selFgColor)));
}

// Each line of a fenced code block is its own QTextBlock; setMarkdown() tags
// it with this property (the fence character) but otherwise leaves it as a
// plain paragraph, so we use it to find code lines for styling/highlighting.
static bool isMarkdownCodeBlock(const QTextBlock &block)
{
    return block.blockFormat().hasProperty(QTextFormat::BlockCodeFence);
}

static QColor markdownCodeBackground()
{
    return QColor(0xf2, 0xf2, 0xf2);
}

// Qt's own markdown heading scale (relative to a 9pt body font: 18/13.5/10.8/9/7.2/6.3
// for h1..h6) makes h4 render at the exact same size as body text and h5/h6 *smaller*
// than body text — those headings are easy to mistake for plain bold text. Replace it
// with a scale that's always larger than body text and clearly stepped.
static qreal markdownHeadingScale(int headingLevel)
{
    switch (headingLevel)
    {
    case 1: return 1.8;
    case 2: return 1.5;
    case 3: return 1.3;
    case 4: return 1.15;
    case 5: return 1.05;
    default: return 1.0;
    }
}

// QTextDocument::setMarkdown() builds blocks directly rather than via HTML/CSS,
// so heading/paragraph spacing has to be applied per-block after the fact.
// Code-block lines are left alone here; wrapMarkdownCodeBlocks() below handles
// their background/spacing via a QTextFrame instead.
static void styleMarkdownBlocks(QTextDocument *doc)
{
    const QFont baseFont = doc->defaultFont();
    QTextCursor cursor(doc);
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next())
    {
        if (isMarkdownCodeBlock(block))
            continue;

        QTextBlockFormat format = block.blockFormat();
        const int headingLevel = format.headingLevel();
        if (headingLevel > 0)
        {
            format.setTopMargin(headingLevel <= 2 ? 22 : 16);
            format.setBottomMargin(10);
        }
        else
        {
            format.setTopMargin(0);
            format.setBottomMargin(12);
        }
        cursor.setPosition(block.position());
        cursor.setBlockFormat(format);

        if (headingLevel > 0)
        {
            QFont headingFont = baseFont;
            headingFont.setBold(true);
            headingFont.setPointSizeF(baseFont.pointSizeF() * markdownHeadingScale(headingLevel));
            QTextCharFormat headingCharFormat;
            headingCharFormat.setFont(headingFont);
            cursor.setPosition(block.position());
            cursor.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
            cursor.mergeCharFormat(headingCharFormat);
        }
    }
}

// QTextBlockFormat margins shift the background along with the text, so they
// can't create a gap between the text and the box edge. A QTextFrame can:
// QTextFrameFormat::setPadding() insets the content from the frame's own
// background/border, giving each fenced code sample a real padded card.
static void wrapMarkdownCodeBlocks(QTextDocument *doc)
{
    QTextBlock block = doc->begin();
    while (block.isValid())
    {
        if (!isMarkdownCodeBlock(block))
        {
            block = block.next();
            continue;
        }

        QTextBlock lastLine = block;
        while (isMarkdownCodeBlock(lastLine.next()))
            lastLine = lastLine.next();

        QTextFrameFormat frameFormat;
        frameFormat.setBackground(markdownCodeBackground());
        frameFormat.setBorderStyle(QTextFrameFormat::BorderStyle_None);
        frameFormat.setPadding(12);
        frameFormat.setTopMargin(14);
        frameFormat.setBottomMargin(14);
        frameFormat.setLeftMargin(0);
        frameFormat.setRightMargin(0);

        QTextCursor selection(doc);
        selection.setPosition(block.position());
        selection.setPosition(lastLine.position() + lastLine.length() - 1, QTextCursor::KeepAnchor);
        QTextFrame *frame = selection.insertFrame(frameFormat);

        // insertFrame() splits the first line of the selection into an empty
        // block left outside the frame and a fresh block holding the text
        // inside it; that fresh block doesn't inherit BlockCodeFence, so
        // restore it on every child block to keep isMarkdownCodeBlock() true.
        QTextCursor fix(doc);
        for (auto it = frame->begin(); !it.atEnd(); ++it)
        {
            const QTextBlock child = it.currentBlock();
            if (!child.isValid() || isMarkdownCodeBlock(child))
                continue;
            QTextBlockFormat childFormat = child.blockFormat();
            childFormat.setProperty(QTextFormat::BlockCodeFence, QStringLiteral("`"));
            fix.setPosition(child.position());
            fix.setBlockFormat(childFormat);
        }

        block = doc->findBlock(frame->lastPosition() + 1);
    }
}

// setMarkdown()'s GFM tables come with Qt's default border/padding; restyle them
// to a flatter, lighter look and shade the header row to set it off from the body.
static void styleMarkdownTables(QTextDocument *doc)
{
    QList<QTextTable *> seen;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next())
    {
        QTextTable *table = QTextCursor(block).currentTable();
        if (!table || seen.contains(table))
            continue;
        seen.append(table);

        QTextTableFormat format = table->format();
        format.setBorder(1);
        format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
        format.setBorderBrush(QColor(0xd0, 0xd0, 0xd0));
        format.setCellPadding(10);
        format.setCellSpacing(0);
        format.setTopMargin(10);
        format.setBottomMargin(10);
        table->setFormat(format);

        for (int col = 0; col < table->columns(); ++col)
        {
            QTextTableCell cell = table->cellAt(0, col);
            QTextCharFormat cellFormat = cell.format();
            cellFormat.setBackground(QColor(0xf0, 0xf0, 0xf0));
            cell.setFormat(cellFormat);
        }
    }
}

static QString typeLibKeywordPattern()
{
    QStringList words = {
        QStringLiteral("char"),
        QStringLiteral("wchar_t"),
        QStringLiteral("byte"),
        QStringLiteral("word"),
        QStringLiteral("dword"),
        QStringLiteral("qword"),
        QStringLiteral("float"),
        QStringLiteral("double"),
    };

#ifdef DEFINE_KEYWORD
#pragma push_macro("DEFINE_KEYWORD")
#undef DEFINE_KEYWORD
#define QEXED_RESTORE_DEFINE_KEYWORD
#endif

#define DEFINE_KEYWORD(tok, str) words.append(QStringLiteral(str));
#include "keywords.h"
#undef DEFINE_KEYWORD

#ifdef QEXED_RESTORE_DEFINE_KEYWORD
#pragma pop_macro("DEFINE_KEYWORD")
#undef QEXED_RESTORE_DEFINE_KEYWORD
#endif

    words.removeDuplicates();
    for (QString &word : words)
        word = QRegularExpression::escape(word);

    return QStringLiteral(R"(\b(?:%1)\b)").arg(words.join(QLatin1Char('|')));
}

static QString typeKeywordPattern()
{
    QStringList words = {
        QStringLiteral("byte"),
        QStringLiteral("word"),
        QStringLiteral("dword"),
        QStringLiteral("qword"),
        QStringLiteral("char"),
        QStringLiteral("wchar_t"),
        QStringLiteral("float"),
        QStringLiteral("double"),
        QStringLiteral("signed"),
        QStringLiteral("unsigned"),
        QStringLiteral("short"),
        QStringLiteral("long"),
        QStringLiteral("int"),
    };

    for (QString &word : words)
        word = QRegularExpression::escape(word);

    return QStringLiteral(R"(\b(?:%1)\b)").arg(words.join(QLatin1Char('|')));
}

static QColor mutedBracketIdentifierColor(const QPalette &palette)
{
    return palette.color(QPalette::Base).lightness() < 128
        ? QColor(202, 221, 88)
        : QColor(116, 145, 28);
}

static QColor structureKeywordColor()
{
    return QColor(0xcf, 0x23, 0x38);
}

static QColor structureStringColor()
{
    return QColor(0x6d, 0x48, 0xc3);
}

static QColor structureTypeNameColor()
{
    return QColor(0x06, 0x51, 0xae);
}

static QColor structureTagKeywordColor(const QPalette &palette)
{
    return palette.color(QPalette::Base).lightness() < 128
        ? QColor(73, 210, 139)
        : QColor(0, 137, 82);
}

struct StructureSourceHighlightColors
{
    QColor keyword;
    QColor typeKeyword;
    QColor string;
    QColor number;
    QColor tagKeyword;
    QColor tagIdentifier;
    QColor typeName;
    QColor comment;

    static StructureSourceHighlightColors fromPalette(const QPalette &palette, HexView *hv)
    {
        StructureSourceHighlightColors colors;
        colors.keyword = structureKeywordColor();
        colors.typeKeyword = structureTypeNameColor();
        colors.string = structureStringColor();
        colors.number = hv ? QColor(hv->getHexColour(HVC_HEXODD))
                           : palette.color(QPalette::WindowText).darker(135);
        colors.tagKeyword = structureTagKeywordColor(palette);
        colors.tagIdentifier = mutedBracketIdentifierColor(palette);
        colors.typeName = structureTypeNameColor();
        colors.comment = filestats::subduedTextColor(palette);
        return colors;
    }
};

class StructureSourceHighlighter : public QSyntaxHighlighter
{
public:
    explicit StructureSourceHighlighter(QTextDocument *document, const QPalette &palette, HexView *hv,
                                         std::function<bool(const QTextBlock &)> blockFilter = nullptr)
        : QSyntaxHighlighter(document)
        , m_blockFilter(std::move(blockFilter))
    {
        const StructureSourceHighlightColors colors = StructureSourceHighlightColors::fromPalette(palette, hv);

        m_keywordFormat.setForeground(colors.keyword);
        m_typeKeywordFormat.setForeground(colors.typeKeyword);
        m_bracketKeywordFormat.setForeground(colors.tagKeyword);
        m_bracketKeywordFormat.setFontWeight(QFont::Normal);
        m_stringFormat.setForeground(colors.string);
        m_bracketStringFormat.setForeground(colors.string);
        m_numberFormat.setForeground(colors.number);
        m_commentFormat.setForeground(colors.comment);
        m_bracketIdentifierFormat.setForeground(colors.tagIdentifier);
        m_bracketIdentifierFormat.setFontWeight(QFont::Normal);
        m_typeNameFormat.setForeground(colors.typeName);
        m_bracketFormat.setFontWeight(QFont::Normal);
        m_tagPunctuationFormat.setForeground(colors.tagKeyword);
        m_tagPunctuationFormat.setFontWeight(QFont::Normal);
        m_outerBracketFormat.setForeground(colors.tagKeyword);
        m_outerBracketFormat.setFontWeight(QFont::Bold);

        m_keywordPattern = QRegularExpression(typeLibKeywordPattern());
        m_typeKeywordPattern = QRegularExpression(typeKeywordPattern());
        m_stringPattern = QRegularExpression(
            QStringLiteral(R"(\"(?:[^\"\\]|\\.)*\"|'(?:[^'\\]|\\.)*')"));
        m_numberPattern = QRegularExpression(
            QStringLiteral(R"(\b(?:0x[0-9a-fA-F]+|\d+(?:\.\d+)?)\b)"));
        m_bracketPattern = QRegularExpression(QStringLiteral(R"([{}])"));
        m_identifierPattern = QRegularExpression(QStringLiteral(R"(\b[A-Za-z_][A-Za-z0-9_]*\b)"));
    }

protected:
    void highlightBlock(const QString &text) override
    {
        if (m_blockFilter && !m_blockFilter(currentBlock()))
            return;

        applyMatches(text, m_keywordPattern, m_keywordFormat);
        applyMatches(text, m_typeKeywordPattern, m_typeKeywordFormat);
        applyMatches(text, m_numberPattern, m_numberFormat);
        applyMatches(text, m_stringPattern, m_stringFormat);
        applyTypeNames(text);
        applyMatches(text, m_bracketPattern, m_bracketFormat);
        applyBracketIdentifiers(text);

        const int commentStart = lineCommentStart(text);
        if (commentStart >= 0)
            setFormat(commentStart, text.size() - commentStart, m_commentFormat);
    }

private:
    void applyMatches(const QString &text, const QRegularExpression &pattern, const QTextCharFormat &format)
    {
        QRegularExpressionMatchIterator it = pattern.globalMatch(text);
        while (it.hasNext())
        {
            const QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), format);
        }
    }

    void applyTypeNames(const QString &text)
    {
        const int commentStart = lineCommentStart(text);
        const int scanEnd = commentStart >= 0 ? commentStart : text.size();
        int statementStart = 0;
        int depth = qMax(0, previousBlockState());
        QChar quote;
        bool escaped = false;

        for (int i = 0; i < scanEnd; ++i)
        {
            const QChar ch = text.at(i);
            if (!quote.isNull())
            {
                if (escaped)
                {
                    escaped = false;
                    continue;
                }
                if (ch == QLatin1Char('\\'))
                {
                    escaped = true;
                    continue;
                }
                if (ch == quote)
                    quote = QChar();
                continue;
            }

            if (ch == QLatin1Char('"') || ch == QLatin1Char('\''))
            {
                quote = ch;
                continue;
            }
            if (ch == QLatin1Char('['))
            {
                ++depth;
                continue;
            }
            if (ch == QLatin1Char(']') && depth > 0)
            {
                --depth;
                continue;
            }
            if (ch == QLatin1Char(';') && depth == 0)
            {
                applyTypeNamesInSpan(text, statementStart, i - statementStart, false);
                statementStart = i + 1;
            }
            else if (ch == QLatin1Char('{') && depth == 0)
            {
                applyTypeNamesInSpan(text, statementStart, i - statementStart, true);
                statementStart = i + 1;
            }
        }
    }

    void applyTypeNamesInSpan(const QString &text, int start, int length, bool opensCompound)
    {
        if (length <= 0)
            return;

        const QString span = text.mid(start, length);
        QList<QRegularExpressionMatch> identifiers;

        QRegularExpressionMatchIterator it = m_identifierPattern.globalMatch(span);
        while (it.hasNext())
        {
            const QRegularExpressionMatch match = it.next();
            const int matchStart = match.capturedStart();
            if (keywordMatch(span, matchStart, match.capturedLength()))
                continue;
            if (spanSquareDepthAt(span, matchStart) > 0)
                continue;

            identifiers.push_back(match);
        }

        if (identifiers.isEmpty())
            return;

        if (span.trimmed().startsWith(QStringLiteral("typedef")))
        {
            for (const QRegularExpressionMatch &match : identifiers)
                setFormat(start + match.capturedStart(), match.capturedLength(), m_typeNameFormat);
            return;
        }

        const int typeCount = opensCompound ? identifiers.size() : qMax(0, identifiers.size() - 1);
        for (int i = 0; i < typeCount; ++i)
        {
            const QRegularExpressionMatch &match = identifiers.at(i);
            setFormat(start + match.capturedStart(), match.capturedLength(), m_typeNameFormat);
        }
    }

    int spanSquareDepthAt(const QString &text, int offset) const
    {
        int depth = 0;
        QChar quote;
        bool escaped = false;

        for (int i = 0; i < offset && i < text.size(); ++i)
        {
            const QChar ch = text.at(i);
            if (!quote.isNull())
            {
                if (escaped)
                {
                    escaped = false;
                    continue;
                }
                if (ch == QLatin1Char('\\'))
                {
                    escaped = true;
                    continue;
                }
                if (ch == quote)
                    quote = QChar();
                continue;
            }

            if (ch == QLatin1Char('"') || ch == QLatin1Char('\''))
                quote = ch;
            else if (ch == QLatin1Char('['))
                ++depth;
            else if (ch == QLatin1Char(']') && depth > 0)
                --depth;
        }

        return depth;
    }

    void applyBracketIdentifiers(const QString &text)
    {
        const int commentStart = lineCommentStart(text);
        const int scanEnd = commentStart >= 0 ? commentStart : text.size();
        int depth = qMax(0, previousBlockState());
        int spanStart = depth > 0 ? 0 : -1;
        QChar quote;
        bool escaped = false;

        for (int i = 0; i < scanEnd; ++i)
        {
            const QChar ch = text.at(i);
            if (!quote.isNull())
            {
                if (escaped)
                {
                    escaped = false;
                    continue;
                }
                if (ch == QLatin1Char('\\'))
                {
                    escaped = true;
                    continue;
                }
                if (ch == quote)
                    quote = QChar();
                continue;
            }

            if (ch == QLatin1Char('"') || ch == QLatin1Char('\''))
            {
                quote = ch;
                continue;
            }

            if (ch == QLatin1Char('['))
            {
                if (depth == 0)
                {
                    spanStart = i + 1;
                    setFormat(i, 1, m_outerBracketFormat);
                }
                ++depth;
            }
            else if (ch == QLatin1Char(']') && depth > 0)
            {
                if (depth == 1)
                    setFormat(i, 1, m_outerBracketFormat);
                --depth;
                if (depth == 0 && spanStart >= 0 && i > spanStart)
                {
                    applyBracketIdentifiersInSpan(text, spanStart, i - spanStart);
                    spanStart = -1;
                }
            }
        }

        if (depth > 0 && spanStart >= 0 && scanEnd > spanStart)
            applyBracketIdentifiersInSpan(text, spanStart, scanEnd - spanStart);

        setCurrentBlockState(depth);
    }

    void applyBracketIdentifiersInSpan(const QString &text, int start, int length)
    {
        const QString span = text.mid(start, length);
        applyTagPunctuationInSpan(span, start);

        QRegularExpressionMatchIterator it = m_identifierPattern.globalMatch(span);
        while (it.hasNext())
        {
            const QRegularExpressionMatch match = it.next();
            const int absoluteStart = start + match.capturedStart();
            const bool isKeyword = keywordMatch(span, match.capturedStart(), match.capturedLength())
                || typeKeywordMatch(span, match.capturedStart(), match.capturedLength());
            setFormat(absoluteStart,
                      match.capturedLength(),
                      isKeyword ? m_bracketKeywordFormat : m_bracketIdentifierFormat);
        }

        applyMatchesInSpan(span, start, m_stringPattern, m_bracketStringFormat);
    }

    void applyTagPunctuationInSpan(const QString &span, int absoluteOffset)
    {
        for (int i = 0; i < span.size(); ++i)
        {
            const QChar ch = span.at(i);
            if (!ch.isLetterOrNumber() && ch != QLatin1Char('_') && !ch.isSpace())
            {
                const bool squareBracket = ch == QLatin1Char('[') || ch == QLatin1Char(']');
                setFormat(absoluteOffset + i, 1, squareBracket ? m_outerBracketFormat : m_tagPunctuationFormat);
            }
        }
    }

    void applyMatchesInSpan(const QString &span, int absoluteOffset, const QRegularExpression &pattern, const QTextCharFormat &format)
    {
        QRegularExpressionMatchIterator it = pattern.globalMatch(span);
        while (it.hasNext())
        {
            const QRegularExpressionMatch match = it.next();
            setFormat(absoluteOffset + match.capturedStart(), match.capturedLength(), format);
        }
    }

    bool keywordMatch(const QString &text, int start, int length) const
    {
        const QRegularExpressionMatch match = m_keywordPattern.match(text, start);
        return match.hasMatch()
            && match.capturedStart() == start
            && match.capturedLength() == length;
    }

    bool typeKeywordMatch(const QString &text, int start, int length) const
    {
        const QRegularExpressionMatch match = m_typeKeywordPattern.match(text, start);
        return match.hasMatch()
            && match.capturedStart() == start
            && match.capturedLength() == length;
    }

    int lineCommentStart(const QString &text) const
    {
        QChar quote;
        bool escaped = false;

        for (int i = 0; i + 1 < text.size(); ++i)
        {
            const QChar ch = text.at(i);
            if (!quote.isNull())
            {
                if (escaped)
                {
                    escaped = false;
                    continue;
                }
                if (ch == QLatin1Char('\\'))
                {
                    escaped = true;
                    continue;
                }
                if (ch == quote)
                    quote = QChar();
                continue;
            }

            if (ch == QLatin1Char('"') || ch == QLatin1Char('\''))
            {
                quote = ch;
                continue;
            }
            if (ch == QLatin1Char('/') && text.at(i + 1) == QLatin1Char('/'))
                return i;
        }

        return -1;
    }

    QRegularExpression m_keywordPattern;
    QRegularExpression m_typeKeywordPattern;
    QRegularExpression m_stringPattern;
    QRegularExpression m_numberPattern;
    QRegularExpression m_bracketPattern;
    QRegularExpression m_identifierPattern;
    QTextCharFormat m_keywordFormat;
    QTextCharFormat m_typeKeywordFormat;
    QTextCharFormat m_bracketKeywordFormat;
    QTextCharFormat m_stringFormat;
    QTextCharFormat m_bracketStringFormat;
    QTextCharFormat m_numberFormat;
    QTextCharFormat m_typeNameFormat;
    QTextCharFormat m_bracketIdentifierFormat;
    QTextCharFormat m_bracketFormat;
    QTextCharFormat m_tagPunctuationFormat;
    QTextCharFormat m_outerBracketFormat;
    QTextCharFormat m_commentFormat;
    std::function<bool(const QTextBlock &)> m_blockFilter;
};

struct LogDiagnosticLink
{
    QString path;
    int     lineNo = 0;
    int     pathStart = -1;
    int     pathLength = 0;

    bool isValid() const
    {
        return !path.isEmpty() && lineNo > 0 && pathStart >= 0 && pathLength > 0;
    }
};

LogDiagnosticLink parseLogDiagnosticLine(const QString &line)
{
    static const QRegularExpression diagnosticPattern(
        QStringLiteral(R"(^\s*(.+)\((\d+)\)\s*:\s*error\b)"),
        QRegularExpression::CaseInsensitiveOption);

    LogDiagnosticLink link;
    const QRegularExpressionMatch match = diagnosticPattern.match(line);
    if (!match.hasMatch())
        return link;

    bool ok = false;
    const int parsedLine = match.captured(2).toInt(&ok);
    if (!ok || parsedLine <= 0)
        return link;

    link.path = match.captured(1).trimmed();
    link.lineNo = parsedLine;
    link.pathStart = match.capturedStart(1);
    link.pathLength = match.capturedLength(1);
    return link;
}

class StructureLogHighlighter : public QSyntaxHighlighter
{
public:
    explicit StructureLogHighlighter(QTextDocument *document)
        : QSyntaxHighlighter(document)
    {
        m_errorFormat.setForeground(QColor(0x8b, 0x00, 0x00));
        m_errorFormat.setFontWeight(QFont::DemiBold);
        m_pathFormat.setForeground(QColor(0x8b, 0x00, 0x00));
        m_pathFormat.setFontWeight(QFont::DemiBold);
        m_pathFormat.setFontUnderline(true);
    }

protected:
    void highlightBlock(const QString &text) override
    {
        if (text.contains(QStringLiteral("error"), Qt::CaseInsensitive))
            setFormat(0, text.size(), m_errorFormat);

        const LogDiagnosticLink link = parseLogDiagnosticLine(text);
        if (link.isValid())
            setFormat(link.pathStart, link.pathLength, m_pathFormat);
    }

private:
    QTextCharFormat m_errorFormat;
    QTextCharFormat m_pathFormat;
};

qreal devicePixelSize(const QPainter *painter)
{
    const qreal dpr = painter && painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    return dpr > 0.0 ? 1.0 / dpr : 1.0;
}

qreal snapToDevicePixel(const QPainter *painter, qreal value)
{
    const qreal dpr = painter && painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    return dpr > 0.0 ? std::round(value * dpr) / dpr : value;
}

bool useClassicPlusMinusExpanders()
{
    return qEnvironmentVariableIntValue("QEXED_STRUCTURE_PLUS_MINUS") != 0;
}

class StructureGridHeader : public QHeaderView
{
public:
    using ContextMenuCallback = std::function<void(int column, const QPoint &globalPos)>;

    explicit StructureGridHeader(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QHeaderView(orientation, parent)
    {
        setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        setHighlightSections(false);
        setSectionsClickable(false);
        setMouseTracking(true);
    }

    void setContextMenuCallback(ContextMenuCallback callback)
    {
        m_contextMenuCallback = std::move(callback);
    }

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override
    {
        if (!painter || !rect.isValid())
            return;

        painter->save();
        const bool hovered = rect.contains(mapFromGlobal(QCursor::pos()));
        const QColor background = hovered ? palette().color(QPalette::Button)
                                          : palette().color(QPalette::Base);
        painter->fillRect(rect, background);

        QFont headerFont = font();
        if (headerFont.pointSizeF() > 0)
            headerFont.setPointSizeF(qMax(1.0, headerFont.pointSizeF() - 1.0));
        else if (headerFont.pixelSize() > 0)
            headerFont.setPixelSize(qMax(1, headerFont.pixelSize() - 1));
        headerFont.setWeight(QFont::DemiBold);

        QStyleOptionHeader opt;
        initStyleOption(&opt);
        initStyleOptionForIndex(&opt, logicalIndex);
        opt.rect = rect.adjusted(0, 0, 0, -kHeaderBottomGap);
        opt.fontMetrics = QFontMetrics(headerFont);
        opt.text.clear();
        opt.sortIndicator = QStyleOptionHeader::None;
        opt.palette.setColor(QPalette::Button, background);
        opt.palette.setColor(QPalette::Window, background);
        style()->drawControl(QStyle::CE_Header, &opt, painter, this);

        const QColor textColor = hovered ? palette().color(QPalette::WindowText)
                                         : filestats::stringsHeaderTextColor(palette());
        const int pad = filestats::stringsHeaderPadding(QFontMetrics(headerFont));
        const QRect textRect = opt.rect.adjusted(pad, pad, -pad, -pad);

        painter->setFont(headerFont);
        painter->setPen(textColor);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                          model() ? model()->headerData(logicalIndex, orientation(), Qt::DisplayRole).toString()
                                  : QString());
        painter->restore();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        QHeaderView::mouseMoveEvent(event);
        if (nearSectionResizeHandle(event->position().toPoint()))
            viewport()->setCursor(Qt::SplitHCursor);
        else if (logicalIndexAt(event->position().toPoint()) >= 0)
            viewport()->setCursor(Qt::PointingHandCursor);
        else
            viewport()->setCursor(Qt::ArrowCursor);
        viewport()->update();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (m_contextMenuCallback
            && (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)
            && !(event->button() == Qt::LeftButton && nearSectionResizeHandle(event->position().toPoint())))
        {
            const int logical = logicalIndexAt(event->position().toPoint());
            if (logical >= 0)
            {
                m_contextMenuCallback(logical, mapToGlobal(event->position().toPoint()));
                event->accept();
                return;
            }
        }

        QHeaderView::mousePressEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        QHeaderView::leaveEvent(event);
        viewport()->unsetCursor();
        viewport()->update();
    }

private:
    bool nearSectionResizeHandle(const QPoint &pos) const
    {
        constexpr int kResizeSlop = 4;
        for (int visual = 0; visual < count() - 1; ++visual)
        {
            const int logical = logicalIndex(visual);
            if (isSectionHidden(logical))
                continue;

            const int edgeX = sectionViewportPosition(logical) + sectionSize(logical);
            if (qAbs(pos.x() - edgeX) <= kResizeSlop)
                return true;
        }
        return false;
    }

    ContextMenuCallback m_contextMenuCallback;
};

class StructureGridView : public QTreeView
{
public:
    explicit StructureGridView(QWidget *parent = nullptr)
        : QTreeView(parent)
    {
    }

protected:
    bool viewportEvent(QEvent *event) override
    {
        switch (event->type())
        {
        case QEvent::MouseButtonPress:
        {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton)
            {
                m_resizeSection = resizeSectionAt(mouse->pos());
                if (m_resizeSection >= 0)
                {
                    m_resizeStartX = mouse->position().x();
                    m_resizeStartWidth = header()->sectionSize(m_resizeSection);
                    return true;
                }
                if (handleIndicatorClick(mouse->pos()))
                    return true;
            }
            break;
        }
        case QEvent::MouseButtonDblClick:
        {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton && toggleAggregateRow(indexAt(mouse->pos())))
                return true;
            break;
        }
        case QEvent::MouseMove:
        {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (m_resizeSection >= 0)
            {
                const int dx = qRound(mouse->position().x() - m_resizeStartX);
                header()->resizeSection(m_resizeSection,
                                        qMax(header()->minimumSectionSize(), m_resizeStartWidth + dx));
                return true;
            }
            viewport()->setCursor(resizeSectionAt(mouse->pos()) >= 0 ? Qt::SplitHCursor : Qt::ArrowCursor);
            updateHoverIndex(indexAt(mouse->pos()));
            break;
        }
        case QEvent::MouseButtonRelease:
        {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (m_resizeSection >= 0)
            {
                m_resizeSection = -1;
                viewport()->setCursor(resizeSectionAt(mouse->pos()) >= 0 ? Qt::SplitHCursor : Qt::ArrowCursor);
                return true;
            }
            break;
        }
        case QEvent::Leave:
            if (m_resizeSection < 0)
                viewport()->unsetCursor();
            updateHoverIndex(QModelIndex());
            break;
        default:
            break;
        }

        return QTreeView::viewportEvent(event);
    }

    void drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        const QColor grid = StructureGridItemDelegate::gridColour(palette());
        const qreal px = devicePixelSize(painter);
        const qreal topY = snapToDevicePixel(painter, rect.top());
        painter->fillRect(QRectF(rect.left(), topY, rect.width(), px), grid);
        if (!indexBelow(index).isValid())
        {
            const qreal bottomY = snapToDevicePixel(painter, rect.bottom() + 1.0) - px;
            painter->fillRect(QRectF(rect.left(), bottomY, rect.width(), px), grid);
        }
        painter->restore();

        const QString closedBranchIcon = index.data(StructureTreeModel::BranchIconPathRole).toString();
        if (!closedBranchIcon.isEmpty())
        {
            const bool expandable = model() && model()->rowCount(index) > 0;
            QString iconPath = closedBranchIcon;
            if (!expandable)
            {
                const QString emptyBranchIcon = index.data(StructureTreeModel::BranchEmptyIconPathRole).toString();
                if (!emptyBranchIcon.isEmpty())
                    iconPath = emptyBranchIcon;
            }
            else if (isExpanded(index))
            {
                const QString openBranchIcon = index.data(StructureTreeModel::BranchOpenIconPathRole).toString();
                if (!openBranchIcon.isEmpty())
                    iconPath = openBranchIcon;
            }
            const QIcon icon(iconPath);
            const int iconSize = qMin(kBranchIconSize, qMax(0, qMin(rect.width(), rect.height()) - 2));
            if (iconSize > 0)
            {
                const int branchSlotWidth = qMin(rect.width(), qMax(1, indentation()));
                const QRect branchSlot(rect.right() - branchSlotWidth + 1,
                                       rect.top(),
                                       branchSlotWidth,
                                       rect.height());
                const QRect iconRect(branchSlot.left() + qMax(0, (branchSlot.width() - iconSize) / 2),
                                     branchSlot.top() + qMax(0, (branchSlot.height() - iconSize) / 2),
                                     iconSize,
                                     iconSize);
                icon.paint(painter,
                           iconRect,
                           Qt::AlignCenter,
                           isEnabled() ? QIcon::Normal : QIcon::Disabled);

                // Draw a '+' (closed) or '−' (open) left of the icon on hover/select.
                const bool canExpand = model()
                    && (model()->rowCount(index) > 0 || model()->canFetchMore(index));
                const bool expanded = isExpanded(index);
                if (canExpand || expanded)
                {
                    const bool hovered = m_hoverIndex.isValid()
                        && m_hoverIndex.parent() == index.parent()
                        && m_hoverIndex.row() == index.row();
                    const bool selected = selectionModel()
                        && selectionModel()->isSelected(index);
                    if (hovered || selected)
                    {
                        constexpr qreal arm = 3.0;
                        const qreal cx = branchSlot.left() - indentation() / 2.0;
                        const qreal cy = rect.top() + rect.height() * 0.5;
                        if (cx - arm >= rect.left())
                        {
                            QColor c = palette().color(QPalette::WindowText);
                            c.setAlphaF(c.alphaF() * 0.5);
                            painter->save();
                            painter->setRenderHint(QPainter::Antialiasing, true);
                            painter->setPen(QPen(c, 1.5, Qt::SolidLine, Qt::RoundCap));
                            painter->drawLine(QLineF(cx - arm, cy, cx + arm, cy));
                            if (!expanded)
                                painter->drawLine(QLineF(cx, cy - arm, cx, cy + arm));
                            painter->restore();
                        }
                    }
                }
            }
            return;
        }

        if (!useClassicPlusMinusExpanders())
        {
            QTreeView::drawBranches(painter, rect, index);
            return;
        }

        if (!model() || !model()->hasChildren(index))
            return;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);

        constexpr int boxSize = 11;
        const QRect box(rect.left() + qMax(0, (rect.width() - boxSize) / 2),
                        rect.top() + qMax(0, (rect.height() - boxSize) / 2),
                        boxSize,
                        boxSize);

        const QColor border = palette().color(QPalette::Mid);
        const QColor fill = palette().color(QPalette::Base);
        const QColor mark = palette().color(QPalette::WindowText);

        painter->fillRect(box, fill);
        painter->setPen(border);
        painter->drawRect(box.adjusted(0, 0, -1, -1));

        painter->setPen(mark);
        const int midX = box.center().x();
        const int midY = box.center().y();
        painter->drawLine(box.left() + 3, midY, box.right() - 3, midY);
        if (!isExpanded(index))
            painter->drawLine(midX, box.top() + 3, midX, box.bottom() - 3);

        painter->restore();
    }

private:
    bool toggleAggregateRow(const QModelIndex &index)
    {
        if (!index.isValid()
            || (index.column() != StructureTreeModel::ValueColumn
                && index.column() != StructureTreeModel::OffsetColumn
                && index.column() != StructureTreeModel::NameColumn))
            return false;

        const QModelIndex rowIndex = index.sibling(index.row(), StructureTreeModel::NameColumn);
        const QModelIndex valueIndex = index.sibling(index.row(), StructureTreeModel::ValueColumn);
        const QString value = valueIndex.data(Qt::DisplayRole).toString().trimmed();
        if (!model()->hasChildren(rowIndex) || !value.startsWith(QLatin1Char('{')))
            return false;
        if (index.column() == StructureTreeModel::NameColumn
            && !rowIndex.data(StructureTreeModel::NameTypePrefixRole).toString().startsWith(QLatin1Char('[')))
            return false;

        setExpanded(rowIndex, !isExpanded(rowIndex));
        return true;
    }

    int resizeSectionAt(const QPoint &pos) const
    {
        constexpr int kResizeSlop = 4;
        const int count = header()->count();
        for (int visual = 0; visual < count - 1; ++visual)
        {
            const int logical = header()->logicalIndex(visual);
            if (header()->isSectionHidden(logical))
                continue;

            const int edgeX = header()->sectionViewportPosition(logical) + header()->sectionSize(logical);
            if (qAbs(pos.x() - edgeX) <= kResizeSlop)
                return logical;
        }

        return -1;
    }

    bool handleIndicatorClick(const QPoint &pos)
    {
        const QModelIndex idx = indexAt(pos);
        if (!idx.isValid() || idx.data(StructureTreeModel::BranchIconPathRole).toString().isEmpty())
            return false;

        // Compute item depth to find the indicator's centre x.
        int depth = 0;
        for (QModelIndex p = idx.parent(); p.isValid(); p = p.parent())
            ++depth;
        if (depth == 0)
            return false;

        // The indicator is centred at branchSlot.left() - indentation()/2,
        // where branchSlot.left() = depth * indentation() in viewport x.
        constexpr int arm = 3;
        constexpr int tolerance = 5;
        const int cx = depth * indentation() - indentation() / 2;
        if (pos.x() < cx - arm - tolerance || pos.x() > cx + arm + tolerance)
            return false;

        const bool canExpand = model()
            && (model()->rowCount(idx) > 0 || model()->canFetchMore(idx));
        if (!canExpand && !isExpanded(idx))
            return false;

        const QModelIndex nameIdx = idx.sibling(idx.row(), 0);
        setExpanded(nameIdx, !isExpanded(nameIdx));
        return true;
    }

    void updateHoverIndex(const QModelIndex &newIndex)
    {
        const QModelIndex normalised = newIndex.sibling(newIndex.row(), 0);
        if (normalised == m_hoverIndex)
            return;
        repaintRowBranchArea(m_hoverIndex);
        m_hoverIndex = QPersistentModelIndex(normalised);
        repaintRowBranchArea(m_hoverIndex);
    }

    void repaintRowBranchArea(const QModelIndex &index)
    {
        if (!index.isValid())
            return;
        const QRect r = visualRect(index);
        if (r.isValid())
            viewport()->update(QRect(0, r.top(), viewport()->width(), r.height()));
    }

    int m_resizeSection = -1;
    qreal m_resizeStartX = 0.0;
    int m_resizeStartWidth = 0;
    QPersistentModelIndex m_hoverIndex;
};
}

class StructureContentFrame : public QWidget
{
public:
    enum class Page
    {
        Struct,
        Source,
        Log,
    };

    enum class TabAlignment
    {
        Left,
        Right,
    };

    explicit StructureContentFrame(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMouseTracking(true);

        m_contentLayout = new QVBoxLayout(this);
        m_contentLayout->setContentsMargins(kBorderWidth, kBorderWidth, kBorderWidth,
                                            kTabHeight + kBorderWidth);
        m_contentLayout->setSpacing(0);
    }

    void setContentWidget(QWidget *widget)
    {
        if (!widget)
            return;
        m_contentLayout->addWidget(widget);
    }

    void setStatusLabel(QLabel *label)
    {
        m_statusLabel = label;
        if (m_statusLabel)
        {
            m_statusLabel->setParent(this);
            m_statusLabel->setWordWrap(false);
        }
        updateFooterChildren();
    }

    void setLogButton(QToolButton *button)
    {
        m_logButton = button;
        if (m_logButton)
            m_logButton->setParent(this);
        updateLogButtonState();
        updateFooterChildren();
    }

    void setTabChangedCallback(std::function<void(Page)> callback)
    {
        m_tabChangedCallback = std::move(callback);
    }

    void setCurrentPage(Page page)
    {
        if (m_currentPage == page)
            return;

        m_currentPage = page;
        update();
    }

    Page currentPage() const
    {
        return m_currentPage;
    }

    void setLogActive(bool active)
    {
        if (m_logActive == active)
            return;

        m_logActive = active;
        updateLogButtonState();
        update();
    }

    QSize sizeHint() const override
    {
        return QSize(420, 320);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        updateTabRects();
        const QColor border = palette().color(QPalette::Mid);
        const QColor base = palette().color(QPalette::Base);
        const QColor footer = palette().color(QPalette::Window);

        painter.fillRect(rect(), footer);
        paintContentBody(&painter, base, border);

        paintLogTab(&painter);
        paintTab(&painter, m_structTabRect, tr("Structure"), Page::Struct);
        paintTab(&painter, m_sourceTabRect, tr("Source"), Page::Source);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        updateTabRects();
        updateFooterChildren();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int hoverTab = tabAt(event->pos());
        if (m_hoverTab != hoverTab)
        {
            m_hoverTab = hoverTab;
            update();
        }
        setCursor(hoverTab >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hoverTab = -1;
        unsetCursor();
        update();
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            const int tab = tabAt(event->pos());
            if (tab >= 0)
            {
                const Page page = tab == 0 ? Page::Struct
                    : tab == 1       ? Page::Source
                                     : Page::Log;
                if (m_tabChangedCallback)
                    m_tabChangedCallback(page);
                event->accept();
                return;
            }
        }

        QWidget::mousePressEvent(event);
    }

private:
    static constexpr int kRadius = 6;
    static constexpr int kBorderWidth = 1;
    static constexpr int kTabHeight = 24;
    static constexpr int kTabHorzPad = 14;
    static constexpr int kLogIconSize = 14;
    static constexpr int kLogButtonSide = 20;
    static constexpr int kFooterPad = 6;
    static constexpr TabAlignment kTabAlignment = TabAlignment::Right;

    int tabTop() const
    {
        return height() - kTabHeight;
    }

    QRectF contentBodyRect() const
    {
        return QRectF(0.5, 0.5, qMax(0, width() - 1), qMax(0, tabTop()));
    }

    void updateTabRects()
    {
        const QFontMetrics metrics(font());
        const int structW = metrics.horizontalAdvance(tr("Structure")) + 2 * kTabHorzPad;
        const int sourceW = metrics.horizontalAdvance(tr("Source")) + 2 * kTabHorzPad;
        const int logW = kLogIconSize + 2 * kTabHorzPad;
        const int y = tabTop();

        if (kTabAlignment == TabAlignment::Right)
        {
            const int sourceX = width() - kFooterPad - sourceW;
            const int structX = sourceX - structW + 1;
            const int logX = structX - logW + 1;
            m_logTabRect = QRect(logX, y, logW, kTabHeight);
            m_structTabRect = QRect(structX, y, structW, kTabHeight);
            m_sourceTabRect = QRect(sourceX, y, sourceW, kTabHeight);
        }
        else
        {
            const int logX = kFooterPad;
            const int structX = logX + logW - 1;
            const int sourceX = structX + structW - 1;
            m_logTabRect = QRect(logX, y, logW, kTabHeight);
            m_structTabRect = QRect(structX, y, structW, kTabHeight);
            m_sourceTabRect = QRect(sourceX, y, sourceW, kTabHeight);
        }
    }

    QRect tabGroupRect() const
    {
        return m_logTabRect.united(m_structTabRect).united(m_sourceTabRect);
    }

    void updateFooterChildren()
    {
        updateTabRects();

        const QRect tabs = tabGroupRect();
        const int footerTop = tabTop();
        QRect labelRect;
        QRect logRect(0, 0, kLogButtonSide, kLogButtonSide);
        logRect.moveCenter(m_logTabRect.center());

        if (kTabAlignment == TabAlignment::Right)
        {
            labelRect = QRect(kFooterPad,
                              footerTop,
                              qMax(0, tabs.left() - kFooterPad),
                              kTabHeight);
        }
        else
        {
            labelRect = QRect(tabs.right() + 1,
                              footerTop,
                              qMax(0, width() - tabs.right() - 1 - kFooterPad),
                              kTabHeight);
        }

        if (m_logButton)
        {
            m_logButton->setGeometry(logRect);
            m_logButton->setVisible(logRect.width() > 0 && logRect.left() >= 0 && logRect.right() < width());
        }

        if (m_statusLabel)
            m_statusLabel->setGeometry(labelRect);
    }

    void updateLogButtonState()
    {
        if (!m_logButton)
            return;

        m_logButton->setProperty("logActive", m_logActive);
        m_logButton->style()->unpolish(m_logButton);
        m_logButton->style()->polish(m_logButton);
        m_logButton->update();
    }

    int tabAt(const QPoint &pos) const
    {
        if (m_logTabRect.contains(pos))
            return 2;
        if (m_structTabRect.contains(pos))
            return 0;
        if (m_sourceTabRect.contains(pos))
            return 1;
        return -1;
    }

    void paintContentBody(QPainter *painter, const QColor &base, const QColor &border)
    {
        if (!painter)
            return;

        const QRectF body = contentBodyRect();
        if (body.width() <= 0.0 || body.height() <= 0.0)
            return;

        QPainterPath fillPath;
        fillPath.moveTo(body.left() + kRadius, body.top());
        fillPath.lineTo(body.right() - kRadius, body.top());
        fillPath.quadTo(body.right(), body.top(), body.right(), body.top() + kRadius);
        fillPath.lineTo(body.right(), body.bottom());
        fillPath.lineTo(body.left() + kRadius, body.bottom());
        fillPath.quadTo(body.left(), body.bottom(), body.left(), body.bottom() - kRadius);
        fillPath.lineTo(body.left(), body.top() + kRadius);
        fillPath.quadTo(body.left(), body.top(), body.left() + kRadius, body.top());
        fillPath.closeSubpath();
        painter->fillPath(fillPath, base);

        QPainterPath borderPath;
        borderPath.moveTo(body.left() + kRadius, body.top());
        borderPath.lineTo(body.right() - kRadius, body.top());
        borderPath.quadTo(body.right(), body.top(), body.right(), body.top() + kRadius);
        borderPath.lineTo(body.right(), body.bottom());

        const QRect activeTab = activeTabRect();
        if (!activeTab.isValid())
        {
            borderPath.lineTo(body.left() + kRadius, body.bottom());
        }
        else
        {
            borderPath.lineTo(activeTab.right() + 0.5, body.bottom());
            borderPath.moveTo(activeTab.left() + 0.5, body.bottom());
            borderPath.lineTo(body.left() + kRadius, body.bottom());
        }

        borderPath.quadTo(body.left(), body.bottom(), body.left(), body.bottom() - kRadius);
        borderPath.lineTo(body.left(), body.top() + kRadius);
        borderPath.quadTo(body.left(), body.top(), body.left() + kRadius, body.top());

        painter->setPen(QPen(border, 1.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(borderPath);
    }

    QRect activeTabRect() const
    {
        if (m_logActive)
            return m_logTabRect;
        return m_currentPage == Page::Struct ? m_structTabRect : m_sourceTabRect;
    }

    void paintLogTab(QPainter *painter)
    {
        if (!m_logActive || !painter || !m_logTabRect.isValid())
            return;

        paintTabChrome(painter, m_logTabRect, palette().color(QPalette::Base), true);
    }

    void paintTabChrome(QPainter *painter, const QRect &rect, const QColor &fill, bool active)
    {
        if (!painter || !rect.isValid())
            return;

        const QColor border = palette().color(QPalette::Mid);
        QRectF tabRect(rect);
        tabRect.adjust(0.5, 0.5, -0.5, -0.5);

        QPainterPath fillPath;
        fillPath.moveTo(tabRect.left(), tabRect.top());
        fillPath.lineTo(tabRect.right(), tabRect.top());
        fillPath.lineTo(tabRect.right(), tabRect.bottom() - kRadius);
        fillPath.quadTo(tabRect.right(), tabRect.bottom(),
                        tabRect.right() - kRadius, tabRect.bottom());
        fillPath.lineTo(tabRect.left() + kRadius, tabRect.bottom());
        fillPath.quadTo(tabRect.left(), tabRect.bottom(),
                        tabRect.left(), tabRect.bottom() - kRadius);
        fillPath.lineTo(tabRect.left(), tabRect.top());
        fillPath.closeSubpath();
        painter->fillPath(fillPath, fill);

        QPainterPath borderPath;
        if (active)
        {
            borderPath.moveTo(tabRect.left(), tabRect.top());
            borderPath.lineTo(tabRect.left(), tabRect.bottom() - kRadius);
            borderPath.quadTo(tabRect.left(), tabRect.bottom(),
                              tabRect.left() + kRadius, tabRect.bottom());
            borderPath.lineTo(tabRect.right() - kRadius, tabRect.bottom());
            borderPath.quadTo(tabRect.right(), tabRect.bottom(),
                              tabRect.right(), tabRect.bottom() - kRadius);
            borderPath.lineTo(tabRect.right(), tabRect.top());
        }
        else
        {
            borderPath.moveTo(tabRect.left(), tabRect.top());
            borderPath.lineTo(tabRect.right(), tabRect.top());
            borderPath.lineTo(tabRect.right(), tabRect.bottom() - kRadius);
            borderPath.quadTo(tabRect.right(), tabRect.bottom(),
                              tabRect.right() - kRadius, tabRect.bottom());
            borderPath.lineTo(tabRect.left() + kRadius, tabRect.bottom());
            borderPath.quadTo(tabRect.left(), tabRect.bottom(),
                              tabRect.left(), tabRect.bottom() - kRadius);
            borderPath.lineTo(tabRect.left(), tabRect.top());
        }

        painter->setPen(QPen(border, 1.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(borderPath);
    }

    void paintTab(QPainter *painter, const QRect &rect, const QString &text, Page page)
    {
        if (!painter || !rect.isValid())
            return;

        const bool active = !m_logActive && m_currentPage == page;
        const bool hovered = tabAt(mapFromGlobal(QCursor::pos())) >= 0
            && ((page == Page::Struct && m_hoverTab == 0) || (page == Page::Source && m_hoverTab == 1));
        const QColor border = palette().color(QPalette::Mid);
        const QColor fill = active ? palette().color(QPalette::Base)
            : hovered ? palette().color(QPalette::Button).lighter(104)
                      : palette().color(QPalette::Button);
        const QColor textColor = active || hovered
            ? palette().color(QPalette::WindowText)
            : filestats::subduedTextColor(palette());

        paintTabChrome(painter, rect, fill, active);

        painter->setPen(textColor);
        painter->drawText(rect.adjusted(kTabHorzPad, 0, -kTabHorzPad, -1),
                          Qt::AlignCenter, text);
    }

    QVBoxLayout *m_contentLayout = nullptr;
    QLabel *m_statusLabel = nullptr;
    QToolButton *m_logButton = nullptr;
    QRect m_logTabRect;
    QRect m_structTabRect;
    QRect m_sourceTabRect;
    Page m_currentPage = Page::Struct;
    bool m_logActive = false;
    int m_hoverTab = -1;
    std::function<void(Page)> m_tabChangedCallback;
};

class SourceViewButton : public QWidget
{
public:
    static constexpr int kBtnSz = 40;

    explicit SourceViewButton(const QString &iconName, const QString &tooltip,
                               std::function<void()> onClick, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_iconName(iconName)
        , m_onClick(std::move(onClick))
    {
        setFixedSize(kBtnSz, kBtnSz);
        setToolTip(tooltip);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // Fill widget background to match editor so corners outside the circle are invisible.
        painter.fillRect(rect(), palette().color(QPalette::Base));

        const bool direct = m_hovered || m_pressed;
        QColor circleColor = palette().color(direct ? QPalette::Mid : QPalette::Window);
        if (direct)
            circleColor = circleColor.darker(125);
        const QColor iconColor = direct ? QColor(Qt::white) : QColor(188, 188, 188);

        const QRectF circle = QRectF(rect()).adjusted(2.5, 2.5, -2.5, -2.5);
        painter.setPen(Qt::NoPen);
        painter.setBrush(circleColor);
        painter.drawEllipse(circle);

        static constexpr int kIconSz = 20;
        const QRect iconRect(width() / 2 - kIconSz / 2, height() / 2 - kIconSz / 2, kIconSz, kIconSz);
        QIcon icon(QStringLiteral(":/icons/actions/") + m_iconName + QStringLiteral(".svg"));
        if (icon.isNull())
            icon = QIcon::fromTheme(m_iconName);
        if (!icon.isNull())
        {
            const QPixmap src = icon.pixmap(iconRect.size());
            if (!src.isNull())
            {
                QPixmap tinted(src.size());
                tinted.setDevicePixelRatio(src.devicePixelRatio());
                tinted.fill(Qt::transparent);
                QPainter p2(&tinted);
                p2.drawPixmap(0, 0, src);
                p2.setCompositionMode(QPainter::CompositionMode_SourceIn);
                p2.fillRect(tinted.rect(), iconColor);
                p2.end();
                painter.drawPixmap(iconRect, tinted);
            }
        }
    }

    void enterEvent(QEnterEvent *event) override
    {
        m_hovered = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hovered = false;
        m_pressed = false;
        update();
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            m_pressed = true;
            update();
            event->accept();
        }
        else
        {
            QWidget::mousePressEvent(event);
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            const bool wasPressed = m_pressed;
            m_pressed = false;
            update();
            if (wasPressed && rect().contains(event->position().toPoint()) && m_onClick)
                m_onClick();
            event->accept();
        }
        else
        {
            QWidget::mouseReleaseEvent(event);
        }
    }

private:
    QString m_iconName;
    std::function<void()> m_onClick;
    bool m_hovered = false;
    bool m_pressed = false;
};

StructureViewPanel::StructureViewPanel(HexView *hv, QWidget *parent)
    : QWidget(parent)
    , m_hv(hv)
    , m_definitions(new StructureDefinitionManager(this))
    , m_model(new StructureTreeModel(this))
{
    buildUi();
}

StructureViewPanel::~StructureViewPanel()
{
    clearHexViewOverlay();
}

void StructureViewPanel::refresh()
{
    if (m_reloadBanner)
        m_reloadBanner->hide();
    m_definitions->reload();
}

void StructureViewPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_definitions->ensureLoaded();
    updateOffsetDisplay();
}

void StructureViewPanel::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    clearHexViewOverlay();
}

void StructureViewPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
    {
        updateTreeSelectionPalette();
        setStatusLabelError(m_definitions && !m_definitions->lastError().isEmpty());
    }
}

bool StructureViewPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (m_sourceView && (watched == m_sourceView || watched == m_sourceView->viewport()))
    {
        if (event->type() == QEvent::Resize)
            repositionSourceViewButtons();
    }
    else if (m_logView && watched == m_logView->viewport())
    {
        if (event->type() == QEvent::MouseMove)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const bool overDiagnosticPath = logDiagnosticAt(mouseEvent->position().toPoint(), nullptr, nullptr);
            m_logView->viewport()->setCursor(overDiagnosticPath ? Qt::PointingHandCursor : Qt::IBeamCursor);
        }
        else if (event->type() == QEvent::Leave)
        {
            m_logView->viewport()->unsetCursor();
        }
        else if (event->type() == QEvent::MouseButtonRelease)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton
                && locateLogDiagnosticAt(mouseEvent->position().toPoint()))
            {
                return true;
            }
        }
    }
    else if (m_statusLabel && watched == m_statusLabel && event->type() == QEvent::MouseButtonRelease)
    {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton
            && m_definitions
            && !m_definitions->lastError().isEmpty())
        {
            showLogPage();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void StructureViewPanel::buildUi()
{
    auto *rootLay = new QVBoxLayout(this);
    rootLay->setContentsMargins(0, filestats::kContentMargin, 0, 0);
    rootLay->setSpacing(0);

    const int scrollBarW = QApplication::style()->pixelMetric(QStyle::PM_ScrollBarExtent);

    auto *header = new filestats::SectionHeader(tr("Structure View"), this);
    header->setCloseMode(true);
    header->setExpandCallback([this]() { emit closeRequested(); });

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, scrollBarW, 0);
    headerRow->setSpacing(0);
    headerRow->addWidget(header);
    rootLay->addLayout(headerRow);

    auto *bannerRow = new QWidget(this);
    auto *bannerLay = new QHBoxLayout(bannerRow);
    bannerLay->setContentsMargins(0, 0, scrollBarW, 0);
    bannerLay->setSpacing(0);

    m_reloadBanner = new filestats::ActionBanner(
        tr("Reload"),
        [this]() { refresh(); },
        bannerRow,
        [] {});
    m_reloadBanner->setObjectName(QStringLiteral("structureDefinitionsChangedBanner"));
    m_reloadBanner->setMessage(tr("Structure definitions changed on disk"));
    bannerLay->addWidget(m_reloadBanner);
    rootLay->addWidget(bannerRow);

    auto *content = new QWidget(this);
    auto *contentLay = new QVBoxLayout(content);
    contentLay->setContentsMargins(filestats::kContentMargin, filestats::kContentMargin,
                                   filestats::kContentMargin + 7, 8);
    contentLay->setSpacing(4);

    auto *optLay = new QHBoxLayout;
    optLay->setContentsMargins(0, 0, 0, 0);
    optLay->setSpacing(6);

    m_rootCombo = new MenuComboBox(content);
    m_rootCombo->setToolTip(tr("Root structure"));
    m_rootCombo->setLeadingIcon(
        recoloredIcon(QStringLiteral("actions/hierarchy1"),
                      filestats::subduedTextColor(palette()), 16));
    const int comboH = qMax(24, static_cast<QComboBox *>(m_rootCombo)->sizeHint().height() - 4);
    m_rootCombo->setFixedHeight(comboH);

    m_offsetEdit = new QLineEdit(content);
    m_offsetEdit->setReadOnly(true);
    m_offsetEdit->setFixedHeight(comboH);
    m_offsetEdit->setPlaceholderText(tr("Offset"));
    const auto existingBtns = m_offsetEdit->findChildren<QToolButton *>();
    m_pinAction = m_offsetEdit->addAction(
        recoloredIcon(QStringLiteral("actions/pin1"),
                      palette().color(QPalette::WindowText), 16),
        QLineEdit::TrailingPosition);
    m_pinAction->setToolTip(tr("Pin offset"));
    for (auto *btn : m_offsetEdit->findChildren<QToolButton *>())
        if (!existingBtns.contains(btn))
            btn->setCursor(Qt::PointingHandCursor);
    const int offsetW = m_offsetEdit->fontMetrics().horizontalAdvance(QStringLiteral("00000000")) + 40
                        + 16 + 8; // icon + Qt's action button right margin
    m_offsetEdit->setFixedWidth(offsetW);

    optLay->addWidget(m_rootCombo, 1);
    optLay->addWidget(m_offsetEdit);
    contentLay->addLayout(optLay);
    contentLay->addSpacing(4);

    m_contentFrame = new StructureContentFrame(content);
    m_contentFrame->setObjectName(QStringLiteral("structureContentFrame"));

    m_viewStack = new QStackedWidget(m_contentFrame);
    m_viewStack->setObjectName(QStringLiteral("structureViewStack"));

    m_tree = new StructureGridView(m_viewStack);
    m_tree->setObjectName(QStringLiteral("structureGrid"));
    m_tree->setModel(m_model);
    m_tree->setItemDelegate(new StructureGridItemDelegate(m_tree));
    auto *gridHeader = new StructureGridHeader(Qt::Horizontal, m_tree);
    gridHeader->setContextMenuCallback([this](int column, const QPoint &globalPos) {
        showHeaderContextMenu(column, globalPos);
    });
    m_tree->setHeader(gridHeader);
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setMouseTracking(true);
    m_tree->setIndentation(18);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->setMinimumSectionSize(48);
    m_tree->header()->setSectionResizeMode(StructureTreeModel::NameColumn, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(StructureTreeModel::ValueColumn, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(StructureTreeModel::OffsetColumn, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(StructureTreeModel::CommentColumn, QHeaderView::Interactive);
    m_tree->header()->resizeSection(StructureTreeModel::NameColumn, 190);
    m_tree->header()->resizeSection(StructureTreeModel::ValueColumn, 90);
    m_tree->header()->resizeSection(StructureTreeModel::OffsetColumn, 84);
    m_tree->header()->resizeSection(StructureTreeModel::CommentColumn, 140);
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &StructureViewPanel::updateHexViewSelection);
    connect(m_tree, &QWidget::customContextMenuRequested,
            this, &StructureViewPanel::showGridContextMenu);
    updateTreeSelectionPalette();
    {
        QFont headerFont = m_tree->header()->font();
        if (headerFont.pointSizeF() > 0)
            headerFont.setPointSizeF(qMax(1.0, headerFont.pointSizeF() - 1.0));
        else if (headerFont.pixelSize() > 0)
            headerFont.setPixelSize(qMax(1, headerFont.pixelSize() - 1));
        headerFont.setWeight(QFont::DemiBold);
        const QFontMetrics metrics(headerFont);
        const int headerPad = filestats::stringsHeaderPadding(metrics);
        constexpr int itemCellInset = 3;
        m_treeItemLeftPad = qMax(0, headerPad - itemCellInset);
        m_tree->header()->setFixedHeight(metrics.height() + 2 * headerPad + kHeaderBottomGap);
        updateTreeSelectionPalette();
    }

    m_sourceView = new QPlainTextEdit(m_viewStack);
    m_sourceView->setReadOnly(false);
    m_sourceView->document()->setDocumentMargin(6.0);
    m_sourceView->setFont(structureSourceViewFont(m_hv ? m_hv->font() : font()));
    m_sourceView->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_sourceView->setObjectName(QStringLiteral("structureSourceView"));
    applyStructureTextViewPalette(m_sourceView, m_hv);
    new StructureSourceHighlighter(m_sourceView->document(), m_sourceView->palette(), m_hv);

    m_sourceSaveButton = new SourceViewButton(
        QStringLiteral("document-save-symbolic"),
        tr("Save structure definition"),
        [this]() {
            if (m_currentSourceFilePath.isEmpty() || !m_sourceView || !m_definitions)
                return;

            QFile file(m_currentSourceFilePath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            {
                setStatusLabelError(true);
                m_statusLabel->setText(
                    tr("Failed to save %1").arg(QFileInfo(m_currentSourceFilePath).fileName()));
                return;
            }
            file.write(m_sourceView->toPlainText().toUtf8());
            file.close();

            const QString savedPath = m_currentSourceFilePath;

            // Suppress the file-watcher notification that would otherwise re-raise
            // the "definitions changed" banner immediately after our own reload.
            m_definitions->suppressNextChangeNotification();
            m_definitions->reload();

            // Other, unrelated definition files can fail independently of the one we
            // just saved, so check specifically for the saved file rather than the
            // overall reload() result: reload() already updated the status label/
            // dropdown for the batch as a whole via definitionsReloaded.
            bool savedFileFailed = false;
            for (const FailedStructureFile &failure : m_definitions->failedFiles())
            {
                if (failure.filePath == savedPath)
                {
                    savedFileFailed = true;
                    break;
                }
            }

            if (savedFileFailed)
            {
                showLogPage();
            }
            else
            {
                setStatusLabelError(false);
                m_statusLabel->setText(tr("%1 saved").arg(QFileInfo(savedPath).fileName()));
            }
        },
        m_sourceView);
    m_sourceHelpButton = new SourceViewButton(
        QStringLiteral("question"),
        tr("Strata language reference"),
        [this]() {
            if (m_helpWindow)
            {
                m_helpWindow->raise();
                m_helpWindow->activateWindow();
                return;
            }

            auto *dlg = new QDialog(window());
            dlg->setWindowTitle(tr("Strata Language Reference"));
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->resize(780, 640);

            QPalette dlgPalette = dlg->palette();
            dlgPalette.setColor(QPalette::Window, Qt::white);
            dlg->setPalette(dlgPalette);

            auto *vl = new QVBoxLayout(dlg);
            vl->setContentsMargins(0, 0, 0, 0);

            auto *view = new QTextEdit(dlg);
            view->setObjectName(QStringLiteral("strataHelpView"));
            view->setReadOnly(true);
            view->setFrameShape(QFrame::NoFrame);

            QPalette viewPalette = view->palette();
            viewPalette.setColor(QPalette::Base, Qt::white);
            viewPalette.setColor(QPalette::Text, Qt::black);
            viewPalette.setColor(QPalette::WindowText, Qt::black);
            viewPalette.setColor(QPalette::Mid, QColor(0xb0, 0xb0, 0xb0));
            view->setPalette(viewPalette);
            // theme.cpp's app-wide "QWidget { color: palette(window-text); }" rule beats the
            // palette set above for any text without its own QTextCharFormat foreground (i.e.
            // every heading/paragraph outside the highlighted code blocks), washing them out
            // in dark mode. A literal colour in a widget-scoped sheet overrides it; a
            // palette() reference here would not, since the app sheet's polish already
            // baked its colour into the role this widget would otherwise resolve. The
            // scrollbar's track and the scroll area's corner widget have the same problem
            // (they inherit the app's default window colour, not this view's white), and the
            // scrollbar's bottom margin is bumped past the global default so the dialog's
            // rounded corner doesn't clip the thumb.
            view->setStyleSheet(QStringLiteral(
                "QTextEdit#strataHelpView { color: black; }"
                "QTextEdit#strataHelpView QScrollBar:vertical { background: white; }"
                "QTextEdit#strataHelpView QScrollBar::handle:vertical { margin: 0px 4px 16px 3px; }"
                "QTextEdit#strataHelpView QScrollBar::add-page:vertical,"
                "QTextEdit#strataHelpView QScrollBar::sub-page:vertical { background: white; }"
                "QTextEdit#strataHelpView QScrollBar:horizontal { background: white; }"
                "QTextEdit#strataHelpView QScrollBar::handle:horizontal { margin: 3px 16px 4px 0px; }"
                "QTextEdit#strataHelpView QScrollBar::add-page:horizontal,"
                "QTextEdit#strataHelpView QScrollBar::sub-page:horizontal { background: white; }"
                "QTextEdit#strataHelpView::corner { background: white; }"));

            QFile f(QStringLiteral(":/docs/README.md"));
            if (f.open(QIODevice::ReadOnly))
                view->setMarkdown(QString::fromUtf8(f.readAll()));
            else
                view->setPlainText(tr("Strata language documentation not available."));

            view->document()->setDocumentMargin(24);
            styleMarkdownBlocks(view->document());
            styleMarkdownTables(view->document());
            // Wrap code blocks into frames before attaching the highlighter: insertFrame()
            // cuts and reinserts the selected text, and the highlighter's incremental
            // contentsChange-driven reformat misses the frame's first line if it's already
            // attached when that happens. Attaching afterwards makes its initial highlight
            // pass run once over the final, settled block structure.
            wrapMarkdownCodeBlocks(view->document());
            // Reuses the same highlighter (and q22-struct.xml colors) as the live
            // source editor, scoped to fenced code blocks via isMarkdownCodeBlock.
            new StructureSourceHighlighter(view->document(), viewPalette, nullptr, isMarkdownCodeBlock);

            vl->addWidget(view);

            // QSyntaxHighlighter's initial rehighlight pass, combined with the QTextFrames
            // just inserted above, corrupts Qt's block layout: many blocks right after a
            // frame get laid out with zero height and visually vanish/overlap (this is what
            // made headings and paragraphs appear "missing" -- they're in the document, just
            // laid out with no height). The corruption only resolves on a *second* layout
            // pass, so defer a full re-dirty to the next event-loop turn, after the dialog's
            // first (corrupting) layout has already happened.
            QTextDocument *helpDoc = view->document();
            QTimer::singleShot(0, view, [helpDoc]() {
                helpDoc->markContentsDirty(0, helpDoc->characterCount());
            });

            m_helpWindow = dlg;
            connect(dlg, &QDialog::destroyed, this, [this]() { m_helpWindow = nullptr; });
            dlg->show();
        },
        m_sourceView);
    m_sourceSaveButton->hide();
    m_sourceHelpButton->hide();
    m_sourceView->installEventFilter(this);
    m_sourceView->viewport()->installEventFilter(this);

    m_logView = new QPlainTextEdit(m_viewStack);
    m_logView->setReadOnly(true);
    m_logView->document()->setDocumentMargin(6.0);
    m_logView->setObjectName(QStringLiteral("structureLogView"));
    m_logView->setMouseTracking(true);
    m_logView->viewport()->setMouseTracking(true);
    applyStructureTextViewPalette(m_logView, m_hv);
    new StructureLogHighlighter(m_logView->document());
    m_logView->viewport()->installEventFilter(this);

    m_loadErrorView = new QLabel(m_viewStack);
    m_loadErrorView->setObjectName(QStringLiteral("structureLoadErrorView"));
    m_loadErrorView->setAlignment(Qt::AlignCenter);
    m_loadErrorView->setWordWrap(true);
    m_loadErrorView->setTextFormat(Qt::RichText);
    m_loadErrorView->setOpenExternalLinks(false);
    m_loadErrorView->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_loadErrorView->setStyleSheet(QStringLiteral(
        "QLabel#structureLoadErrorView { background: palette(base); }"));
    connect(m_loadErrorView, &QLabel::linkActivated,
            this, [this](const QString &) { showSourcePage(nullptr); });

    m_viewStack->addWidget(m_tree);
    m_viewStack->addWidget(m_sourceView);
    m_viewStack->addWidget(m_logView);
    m_viewStack->addWidget(m_loadErrorView);

    m_contentFrame->setContentWidget(m_viewStack);

    m_logButton = new QToolButton(m_contentFrame);
    m_logButton->setObjectName(QStringLiteral("structureLogTabButton"));
    m_logButton->setAutoRaise(true);
    m_logButton->setCursor(Qt::PointingHandCursor);
    constexpr int logIconSize = 14;
    m_logButton->setIcon(recoloredIcon(QStringLiteral("actions/logs"),
                                       palette().color(QPalette::WindowText), logIconSize));
    m_logButton->setIconSize(QSize(logIconSize, logIconSize));
    m_logButton->setToolTip(tr("Show definition log"));
    m_logButton->setStyleSheet(QStringLiteral(R"(
        QToolButton#structureLogTabButton {
            background: transparent;
            border: none;
            border-radius: 6px;
            padding: 2px;
        }
        QToolButton#structureLogTabButton[logActive="false"]:hover {
            background: palette(button);
        }
        QToolButton#structureLogTabButton[logActive="false"]:pressed {
            background: palette(midlight);
        }
        QToolButton#structureLogTabButton[logActive="true"]:hover,
        QToolButton#structureLogTabButton[logActive="true"]:pressed {
            background: transparent;
        }
    )"));

    m_statusLabel = new QLabel(m_contentFrame);
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_statusLabel->installEventFilter(this);
    m_contentFrame->setStatusLabel(m_statusLabel);
    m_contentFrame->setLogButton(m_logButton);
    m_contentFrame->setTabChangedCallback([this](StructureContentFrame::Page page) {
        if (page == StructureContentFrame::Page::Struct)
            showGridPage();
        else if (page == StructureContentFrame::Page::Source)
            showSourcePage(selectedRootType());
        else
            showLogPage();
    });

    contentLay->addWidget(m_contentFrame, 1);

    rootLay->addWidget(content, 1);

    connect(m_definitions, &StructureDefinitionManager::definitionsReloaded,
            this, [this]() {
                if (m_reloadBanner)
                    m_reloadBanner->hide();
                updateDefinitionsUi();
                showGridPage();
            });
    connect(m_definitions, &StructureDefinitionManager::definitionFilesChanged,
            this, [this]() {
                if (m_reloadBanner)
                    m_reloadBanner->show();
            });
    connect(m_hv, &HexView::cursorChanged,
            this, [this](size_w) {
                if (m_updatingHexViewFromStructure)
                    return;
                updateOffsetDisplay();
                uint64_t explicitOffset = 0;
                if (!m_pinned && !explicitRootOffset(selectedRootType(), &explicitOffset))
                    rebuildRows();
            });
    connect(m_hv, &HexView::contentChanged,
            this, [this](size_w, size_w, uint) {
                updateOffsetDisplay();
                if (!m_pinned)
                    rebuildRows();
            });
    connect(m_hv, &HexView::fileOpened,
            this, [this](const QString &) { refreshForCurrentFileAssociation(); });
    connect(m_pinAction, &QAction::triggered,
            this, [this]() { setPinned(!m_pinned); });
    connect(m_logButton, &QToolButton::clicked,
            this, [this]() {
                if (m_viewStack && m_viewStack->currentWidget() == m_logView)
                {
                    if (m_contentFrame && m_contentFrame->currentPage() == StructureContentFrame::Page::Source)
                        showSourcePage(selectedRootType());
                    else
                        showGridPage();
                }
                else
                {
                    showLogPage();
                }
            });
    connect(m_rootCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                rebuildRows();
                // Refresh which widget occupies the Struct slot (tree vs. load-error
                // message) if that's what's currently showing; leave Source/Log alone.
                if (m_viewStack
                    && (m_viewStack->currentWidget() == m_tree
                        || m_viewStack->currentWidget() == m_loadErrorView))
                    showGridPage();
            });
}

void StructureViewPanel::updateTreeSelectionPalette()
{
    if (!m_tree || !m_hv)
        return;

    const QColor activeSelection = QColor(m_hv->getHexColour(HVC_SELECTION));
    const QColor activeText = QColor(m_hv->getHexColour(HVC_SELTEXT));
    const QColor inactiveSelection = QColor(m_hv->getHexColour(HVC_SELECTION_INACTIVE));
    const QColor inactiveText = QColor(m_hv->getHexColour(HVC_SELTEXT_INACTIVE));

    QPalette pal = m_tree->palette();
    pal.setColor(QPalette::Active, QPalette::Highlight, activeSelection);
    pal.setColor(QPalette::Active, QPalette::HighlightedText, activeText);
    pal.setColor(QPalette::Inactive, QPalette::Highlight, inactiveSelection);
    pal.setColor(QPalette::Inactive, QPalette::HighlightedText, inactiveText);
    m_tree->setPalette(pal);
    if (m_tree->viewport())
        m_tree->viewport()->setPalette(pal);

    m_tree->setStyleSheet(
        QStringLiteral(R"(
        QTreeView#structureGrid {
            background: palette(base);
            border: none;
            border-radius: 5px;
            padding: 0px;
            outline: none;
        }
        QTreeView#structureGrid::viewport {
            background: palette(base);
            border-radius: 5px;
        }
        QTreeView#structureGrid QHeaderView::section {
            background: palette(base);
            border: none;
            padding: 4px 6px;
        }
        QTreeView#structureGrid QHeaderView::section:hover {
            background: palette(button);
        }
        QTreeView#structureGrid::item {
            padding: 3px 6px 3px %1px;
        }
        QTreeView#structureGrid::item:hover {
            background: palette(button);
        }
        QTreeView#structureGrid::item:selected {
            background: %2;
            color: %3;
        }
        QTreeView#structureGrid::item:selected:!active {
            background: %4;
            color: %5;
        }
    )").arg(m_treeItemLeftPad)
            .arg(filestats::cssColor(activeSelection),
                 filestats::cssColor(activeText),
                 filestats::cssColor(inactiveSelection),
                 filestats::cssColor(inactiveText)));
}

void StructureViewPanel::updateDefinitionsUi()
{
    const QList<ExportedStructureType> exportedTypes = m_definitions->exportedTypes();
    const QList<FailedStructureFile> failedFiles = m_definitions->failedFiles();

    {
        const QSignalBlocker blocker(m_rootCombo);
        m_rootCombo->clear();
        for (const ExportedStructureType &type : exportedTypes)
        {
            const QString displayName = type.description.isEmpty()
                ? displayNameForTypeDecl(type.typeDecl)
                : type.description;
            m_rootCombo->addItem(displayName,
                                 QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(type.typeDecl)));
        }
        // Files that failed to parse still get an entry (by filename, since we never
        // got far enough to read an @export description) so they aren't silently
        // dropped from the list; selecting one shows an error in place of the grid.
        for (const FailedStructureFile &failure : failedFiles)
        {
            const int index = m_rootCombo->count();
            m_rootCombo->addItem(failure.fileName, QVariant::fromValue<qulonglong>(0));
            m_rootCombo->setItemData(index, failure.filePath, kRootComboFilePathRole);
            m_rootCombo->setItemData(index, failure.message, kRootComboErrorRole);
        }

        const int associatedIndex = associatedRootTypeIndex(exportedTypes);
        if (associatedIndex >= 0)
            m_rootCombo->setCurrentIndex(associatedIndex);
        else if (m_hv && !m_hv->filePath().isEmpty())
            m_rootCombo->setCurrentIndex(-1);
    }

    setStatusLabelError(!failedFiles.isEmpty());
    if (!failedFiles.isEmpty())
        m_statusLabel->setText(m_definitions->lastError());
    else
        m_statusLabel->setText(tr("%1 definition file(s), %2 exported type(s)")
                                    .arg(m_definitions->definitionFiles().size())
                                    .arg(exportedTypes.size()));

    rebuildRows();
}

void StructureViewPanel::updateOffsetDisplay()
{
    if (!m_offsetEdit || !m_hv || m_pinned)
        return;

    uint64_t explicitOffset = 0;
    const uint64_t offset = explicitRootOffset(selectedRootType(), &explicitOffset)
        ? explicitOffset
        : m_hv->cursorOffset();
    m_offsetEdit->setText(QString::number(offset, 16).toUpper().rightJustified(8, QLatin1Char('0')));
}

void StructureViewPanel::updatePinAction()
{
    if (!m_pinAction || !m_offsetEdit)
        return;

    const QString iconName = m_pinned ? QStringLiteral("actions/pin0") : QStringLiteral("actions/pin1");
    const QColor iconColor = m_pinned
        ? filestats::subduedTextColor(m_offsetEdit->palette())
        : m_offsetEdit->palette().color(QPalette::WindowText);
    m_pinAction->setIcon(recoloredIcon(iconName, iconColor, 16));
    m_pinAction->setToolTip(m_pinned ? tr("Unpin offset") : tr("Pin offset"));
}

void StructureViewPanel::setPinned(bool pinned)
{
    m_pinned = pinned;
    if (pinned && m_hv)
        m_pinnedOffset = m_hv->cursorOffset();
    updatePinAction();
    if (!pinned)
        updateOffsetDisplay();
    rebuildRows();
}

void StructureViewPanel::rebuildRows()
{
    if (!m_model || !m_hv || !m_definitions || !m_definitions->library())
        return;

    const bool profile = structureProfileEnabled();
    QElapsedTimer totalTimer;
    QElapsedTimer phaseTimer;
    if (profile)
    {
        totalTimer.start();
        phaseTimer.start();
    }

    TypeDecl *rootType = selectedRootType();
    if (!rootType)
    {
        m_model->clear();
        clearHexViewOverlay();
        if (m_hv)
            m_hv->notifyStructureEntryPoint(false, 0);
        return;
    }

    uint64_t explicitOffset = 0;
    const bool hasExplicitOffset = explicitRootOffset(rootType, &explicitOffset);
    if (hasExplicitOffset && m_pinned)
    {
        m_pinned = false;
        updatePinAction();
    }
    const uint64_t baseOffset = hasExplicitOffset ? explicitOffset
        : (m_pinned ? m_pinnedOffset : m_hv->cursorOffset());
    m_offsetEdit->setText(QString::number(baseOffset, 16).toUpper().rightJustified(8, QLatin1Char('0')));
    StructureValueBuilder builder;
    const StructureDisplayOptions options = displayOptions();
    m_rebuildingRows = true;
    auto rows = builder.build(m_definitions->library(),
                              rootType,
                              baseOffset,
                              [this](uint64_t offset, uint8_t *buffer, size_t length) -> size_t {
                                  return m_hv ? m_hv->getData(static_cast<size_w>(offset), buffer, length) : 0;
                              },
                              options);
    const size_t rowCount = profile ? structureRowCount(rows) : 0;
    if (profile)
    {
        structureProfileLog(QStringLiteral("[StructureProfile] panel build rows=%1 ms=%2")
                                .arg(rowCount)
                                .arg(phaseTimer.restart()));
    }
    m_model->setRows(std::move(rows));
    if (profile)
    {
        structureProfileLog(QStringLiteral("[StructureProfile] panel model reset ms=%1")
                                .arg(phaseTimer.restart()));
    }
    m_model->applyDisplayOptions(options);
    if (profile)
    {
        structureProfileLog(QStringLiteral("[StructureProfile] panel display options ms=%1")
                                .arg(phaseTimer.restart()));
    }
    applyInitialExpansion();
    if (profile)
    {
        structureProfileLog(QStringLiteral("[StructureProfile] panel expansion ms=%1")
                                .arg(phaseTimer.restart()));
    }
    m_rebuildingRows = false;
    clearHexViewOverlay();

    if (m_hv)
    {
        uint64_t ep = 0;
        m_hv->notifyStructureEntryPoint(findFirstCodeTarget(m_model, QModelIndex(), &ep), ep);
    }

    if (profile)
    {
        structureProfileLog(QStringLiteral("[StructureProfile] panel total ms=%1")
                                .arg(totalTimer.elapsed()));
    }
}

void StructureViewPanel::applyInitialExpansion()
{
    if (!m_tree || !m_model)
        return;

    m_tree->collapseAll();

    switch (kInitialStructureExpansion)
    {
    case InitialStructureExpansion::Collapsed:
        return;
    case InitialStructureExpansion::All:
        m_tree->expandAll();
        return;
    case InitialStructureExpansion::FirstLevel:
    case InitialStructureExpansion::FirstLevelAndFirstField:
        break;
    }

    const int rootRows = m_model->rowCount();
    for (int row = 0; row < rootRows; ++row)
    {
        const QModelIndex rootIndex = m_model->index(row, StructureTreeModel::NameColumn);
        if (!rootIndex.isValid())
            continue;

        m_tree->expand(rootIndex);

        if (kInitialStructureExpansion == InitialStructureExpansion::FirstLevelAndFirstField)
        {
            const QModelIndex firstField = m_model->index(0, StructureTreeModel::NameColumn, rootIndex);
            if (firstField.isValid())
                m_tree->expand(firstField);
        }
    }
}

void StructureViewPanel::showGridContextMenu(const QPoint &pos)
{
    const QModelIndex rowIndex = m_tree ? m_tree->indexAt(pos).siblingAtColumn(StructureTreeModel::NameColumn) : QModelIndex();
    showOptionsContextMenu(-1, m_tree ? m_tree->mapToGlobal(pos) : mapToGlobal(pos), true, rowIndex);
}

void StructureViewPanel::showHeaderContextMenu(int column, const QPoint &globalPos)
{
    showOptionsContextMenu(column, globalPos, false);
}

void StructureViewPanel::showOptionsContextMenu(int column, const QPoint &globalPos, bool includeAllColumns, const QModelIndex &rowIndex)
{
    QMenu menu(this);

    const auto addSeparatorIfNeeded = [&menu]() {
        if (!menu.actions().isEmpty())
            menu.addSeparator();
    };

    if (rowIndex.isValid() && m_model && m_model->hasChildren(rowIndex))
    {
        QAction *expand = menu.addAction(tr("Expand"));
        connect(expand, &QAction::triggered,
                this, [this, rowIndex]() { m_tree->expand(rowIndex); });

        QAction *collapse = menu.addAction(tr("Collapse"));
        connect(collapse, &QAction::triggered,
                this, [this, rowIndex]() { m_tree->collapse(rowIndex); });

        QAction *expandTree = menu.addAction(tr("Expand subtree"));
        connect(expandTree, &QAction::triggered,
                this, [this, rowIndex]() { expandSubtree(rowIndex); });

        QAction *collapseTree = menu.addAction(tr("Collapse subtree"));
        connect(collapseTree, &QAction::triggered,
                this, [this, rowIndex]() { collapseSubtree(rowIndex); });
    }

    if (sourceRowForIndex(rowIndex))
    {
        addSeparatorIfNeeded();
        QAction *locateSource = menu.addAction(tr("Locate in source"));
        connect(locateSource, &QAction::triggered,
                this, [this, rowIndex]() { locateIndexInSource(rowIndex); });
    }

    {
        StructureRow *row = m_model && rowIndex.isValid() ? m_model->rowForIndex(rowIndex) : nullptr;
        addSeparatorIfNeeded();
        QAction *openCode = menu.addAction(tr("Open in Disassembler"));
        const bool hasTarget = row && row->hasCodeTarget && m_hv;
        openCode->setEnabled(hasTarget);
        if (hasTarget)
        {
            connect(openCode, &QAction::triggered,
                    this, [this, row]() {
                        const size_w offset = static_cast<size_w>(row->codeTargetOffset);
                        m_hv->setCurSel(offset, offset, true);
                        m_hv->scrollCenterIfOffScreen(offset, 1);
                        emit openDisassemblerRequested();
                    });
        }
    }

    if (includeAllColumns || column == StructureTreeModel::NameColumn)
    {
        addSeparatorIfNeeded();
        QAction *definedTypes = menu.addAction(tr("Use defined type names"));
        definedTypes->setCheckable(true);
        definedTypes->setChecked(m_useDefinedTypeNames);
        connect(definedTypes, &QAction::triggered,
                this, &StructureViewPanel::setUseDefinedTypeNames);
    }

    if (includeAllColumns || column == StructureTreeModel::ValueColumn)
    {
        addSeparatorIfNeeded();
        QAction *hexadecimalValues = menu.addAction(includeAllColumns ? tr("Hexadecimal values") : tr("Hexadecimal"));
        hexadecimalValues->setCheckable(true);
        hexadecimalValues->setChecked(m_useHexadecimalValues);
        connect(hexadecimalValues, &QAction::triggered,
                this, &StructureViewPanel::setUseHexadecimalValues);
    }

    if (includeAllColumns || column == StructureTreeModel::OffsetColumn)
    {
        addSeparatorIfNeeded();
        QAction *hexadecimalOffsets = menu.addAction(includeAllColumns ? tr("Hexadecimal offsets") : tr("Hexadecimal"));
        hexadecimalOffsets->setCheckable(true);
        hexadecimalOffsets->setChecked(m_useHexadecimalOffsets);
        connect(hexadecimalOffsets, &QAction::triggered,
                this, &StructureViewPanel::setUseHexadecimalOffsets);

        QAction *relativeOffsets = menu.addAction(tr("Relative offsets"));
        relativeOffsets->setCheckable(true);
        relativeOffsets->setChecked(m_useRelativeOffsets);
        connect(relativeOffsets, &QAction::triggered,
                this, &StructureViewPanel::setUseRelativeOffsets);
    }

    if (!menu.actions().isEmpty())
        menu.exec(globalPos);
}

void StructureViewPanel::expandSubtree(const QModelIndex &index)
{
    if (!m_tree || !m_model || !index.isValid())
        return;

    if (m_model->canFetchMore(index))
        return;

    m_tree->expand(index);
    const int rows = m_model->rowCount(index);
    for (int row = 0; row < rows; ++row)
        expandSubtree(m_model->index(row, StructureTreeModel::NameColumn, index));
}

void StructureViewPanel::collapseSubtree(const QModelIndex &index)
{
    if (!m_tree || !m_model || !index.isValid())
        return;

    const int rows = m_model->rowCount(index);
    for (int row = 0; row < rows; ++row)
        collapseSubtree(m_model->index(row, StructureTreeModel::NameColumn, index));
    m_tree->collapse(index);
}

void StructureViewPanel::repositionSourceViewButtons()
{
    if (!m_sourceSaveButton || !m_sourceHelpButton || !m_sourceView)
        return;

    const QWidget *vp = m_sourceView->viewport();
    if (!vp)
        return;

    // Buttons are children of m_sourceView; position them relative to the
    // viewport's rect mapped into m_sourceView's coordinate space so they
    // stay pinned to the viewport's bottom-right corner regardless of scrolling
    // or whether the scrollbars are shown.
    constexpr int margin = 8;
    constexpr int gap = 6;
    constexpr int btnSz = SourceViewButton::kBtnSz;

    const QPoint vpOrigin = vp->mapTo(m_sourceView, QPoint(0, 0));
    const int x = vpOrigin.x() + vp->width() - margin - btnSz;
    const int bottomY = vpOrigin.y() + vp->height() - margin - btnSz;
    const int topY    = bottomY - gap - btnSz;

    m_sourceSaveButton->move(x, topY);
    m_sourceHelpButton->move(x, bottomY);
}

void StructureViewPanel::showGridPage()
{
    if (m_viewStack && m_tree)
    {
        if (m_loadErrorView && selectedRootHasLoadError(nullptr, nullptr))
        {
            updateLoadErrorView();
            m_viewStack->setCurrentWidget(m_loadErrorView);
        }
        else
        {
            m_viewStack->setCurrentWidget(m_tree);
        }
    }
    if (m_contentFrame)
    {
        m_contentFrame->setCurrentPage(StructureContentFrame::Page::Struct);
        m_contentFrame->setLogActive(false);
    }
}

void StructureViewPanel::showSourcePage(TypeDecl *typeDecl)
{
    if (!m_viewStack || !m_sourceView)
        return;

    QString path;
    int line = 0;
    if (typeDecl && typeDecl->fileRef.fileDesc)
    {
        path = QString::fromLocal8Bit(typeDecl->fileRef.fileDesc->filePath);
        line = static_cast<int>(typeDecl->fileRef.lineNo);
    }
    else
    {
        // No TypeDecl to source from (e.g. a definition file that failed to parse):
        // fall back to whatever the combo's current entry has on file, so "View
        // source" and the Source tab still work for it.
        QString message;
        if (selectedRootHasLoadError(&path, &message))
            line = parseLogDiagnosticLine(message).lineNo;
    }

    if (loadSourceFile(path, line))
    {
        m_viewStack->setCurrentWidget(m_sourceView);
        if (m_contentFrame)
        {
            m_contentFrame->setCurrentPage(StructureContentFrame::Page::Source);
            m_contentFrame->setLogActive(false);
        }
        repositionSourceViewButtons();
        if (m_sourceSaveButton)
        {
            m_sourceSaveButton->show();
            m_sourceSaveButton->raise();
        }
        if (m_sourceHelpButton)
        {
            m_sourceHelpButton->show();
            m_sourceHelpButton->raise();
        }
    }
}

void StructureViewPanel::showLogPage()
{
    if (!m_viewStack || !m_logView || !m_definitions)
        return;

    m_logView->setPlainText(m_definitions->loadLog());
    m_viewStack->setCurrentWidget(m_logView);
    if (m_contentFrame)
        m_contentFrame->setLogActive(true);
}

void StructureViewPanel::updateContentFramePage()
{
    if (!m_contentFrame || !m_viewStack)
        return;

    if (m_viewStack->currentWidget() == m_logView)
    {
        m_contentFrame->setLogActive(true);
        return;
    }

    m_contentFrame->setLogActive(false);
    m_contentFrame->setCurrentPage(m_viewStack->currentWidget() == m_sourceView
                                       ? StructureContentFrame::Page::Source
                                       : StructureContentFrame::Page::Struct);
}

void StructureViewPanel::locateIndexInSource(const QModelIndex &index)
{
    StructureRow *row = sourceRowForIndex(index);
    if (!row)
        return;

    int selStart = -1;
    int selEnd = -1;
    if (const TypeDecl *decl = row->typeDecl)
    {
        // fileRef.wspEnd: for nested fields this is already post-tags (the field keyword).
        // For top-level decls tagRef is unset, so fileRef.wspEnd is pre-tags — handled
        // in loadSourceFile by scanning past @-tag lines.
        if (decl->fileRef.fileDesc && decl->fileRef.wspEnd > 0)
            selStart = static_cast<int>(decl->fileRef.wspEnd);
        // postRef.wspStart is the byte offset immediately after the closing ';'
        if (decl->postRef.fileDesc)
            selEnd = static_cast<int>(decl->postRef.wspStart);
    }

    if (loadSourceFile(row->sourcePath, row->sourceLine, selStart, selEnd)
        && m_viewStack && m_sourceView)
    {
        m_viewStack->setCurrentWidget(m_sourceView);
        updateContentFramePage();
    }
}

bool StructureViewPanel::locateLogDiagnosticAt(const QPoint &viewportPos)
{
    QString path;
    int line = 0;
    if (!logDiagnosticAt(viewportPos, &path, &line))
        return false;

    if (!loadSourceFile(path, line) || !m_viewStack || !m_sourceView)
        return false;

    m_viewStack->setCurrentWidget(m_sourceView);
    updateContentFramePage();
    return true;
}

bool StructureViewPanel::logDiagnosticAt(const QPoint &viewportPos, QString *path, int *lineNo) const
{
    if (!m_logView)
        return false;

    const QTextCursor cursor = m_logView->cursorForPosition(viewportPos);
    const QTextBlock block = cursor.block();
    if (!block.isValid())
        return false;

    const LogDiagnosticLink link = parseLogDiagnosticLine(block.text());
    if (!link.isValid())
        return false;

    const int column = cursor.position() - block.position();
    if (column < link.pathStart || column >= link.pathStart + link.pathLength)
        return false;

    if (path)
        *path = link.path;
    if (lineNo)
        *lineNo = link.lineNo;
    return true;
}

bool StructureViewPanel::loadSourceFile(const QString &path, int line, int selStart, int selEnd)
{
    if (!m_sourceView || path.isEmpty())
        return false;

    QFile file(path);
    // Read in binary mode so byte offsets from the parser (which also reads binary)
    // correspond 1-to-1 with positions in the raw buffer.
    if (!file.open(QIODevice::ReadOnly))
    {
        m_currentSourceFilePath.clear();
        m_sourceView->setPlainText(tr("Unable to open %1").arg(QDir::toNativeSeparators(path)));
        return true;
    }

    m_currentSourceFilePath = path;
    const QByteArray raw = file.readAll();
    m_sourceView->setPlainText(QString::fromUtf8(raw));

    // Map a raw byte offset to a QTextDocument character position, accounting for
    // \r\n → \n normalisation performed by setPlainText (each \r is dropped).
    const auto byteToDocPos = [&raw](int byteOffset) -> int {
        const int limit = qMin(byteOffset, static_cast<int>(raw.size()));
        int crCount = 0;
        for (int i = 0; i < limit; ++i)
            if (raw[i] == '\r')
                ++crCount;
        return byteOffset - crCount;
    };

    // If we have a valid selection range, advance selStart past any leading @tag lines
    // (needed for top-level declarations where fileRef.wspEnd still precedes the tags).
    if (selStart >= 0 && selEnd > selStart)
    {
        int pos = selStart;
        while (pos < selEnd && pos < static_cast<int>(raw.size()))
        {
            int lineStart = pos;
            // Skip horizontal whitespace
            while (lineStart < static_cast<int>(raw.size())
                   && (raw[lineStart] == ' ' || raw[lineStart] == '\t'))
                ++lineStart;
            if (lineStart < static_cast<int>(raw.size()) && raw[lineStart] == '@')
            {
                // Skip to the end of this @tag line
                pos = lineStart;
                while (pos < static_cast<int>(raw.size())
                       && raw[pos] != '\n' && raw[pos] != '\r')
                    ++pos;
                if (pos < static_cast<int>(raw.size()) && raw[pos] == '\r')
                    ++pos;
                if (pos < static_cast<int>(raw.size()) && raw[pos] == '\n')
                    ++pos;
            }
            else
            {
                pos = lineStart;
                break;
            }
        }
        selStart = pos;
    }

    // Apply the selection first so centerCursor scrolls to it.
    if (selStart >= 0 && selEnd > selStart)
    {
        QTextCursor cursor = m_sourceView->textCursor();
        cursor.setPosition(byteToDocPos(selStart));
        cursor.setPosition(byteToDocPos(selEnd), QTextCursor::KeepAnchor);
        m_sourceView->setTextCursor(cursor);
        m_sourceView->centerCursor();
    }
    else if (line > 0)
    {
        QTextBlock block = m_sourceView->document()->findBlockByNumber(line - 1);
        if (block.isValid())
        {
            QTextCursor cursor(block);
            m_sourceView->setTextCursor(cursor);
            m_sourceView->centerCursor();
        }
    }
    return true;
}

void StructureViewPanel::setStatusLabelError(bool error)
{
    if (!m_statusLabel)
        return;

    if (!error)
    {
        m_statusLabel->setStyleSheet(QString());
        m_statusLabel->unsetCursor();
        return;
    }

    m_statusLabel->setCursor(Qt::PointingHandCursor);
    m_statusLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(filestats::cssColor(errorColour())));
}

StructureRow *StructureViewPanel::sourceRowForIndex(const QModelIndex &index) const
{
    if (!m_model || !index.isValid())
        return nullptr;

    StructureRow *row = m_model->rowForIndex(index);
    while (row)
    {
        if (!row->sourcePath.isEmpty())
            return row;
        row = row->parent;
    }
    return nullptr;
}

StructureDisplayOptions StructureViewPanel::displayOptions() const
{
    StructureDisplayOptions options;
    options.typeNameMode = m_useDefinedTypeNames
        ? StructureTypeNameMode::Defined
        : StructureTypeNameMode::Storage;
    options.hexadecimalValues = m_useHexadecimalValues;
    options.hexadecimalOffsets = m_useHexadecimalOffsets;
    options.relativeOffsets = m_useRelativeOffsets;
    return options;
}

void StructureViewPanel::applyDisplayOptions()
{
    if (m_model)
        m_model->applyDisplayOptions(displayOptions());
}

void StructureViewPanel::setUseDefinedTypeNames(bool enabled)
{
    if (m_useDefinedTypeNames == enabled)
        return;

    m_useDefinedTypeNames = enabled;
    applyDisplayOptions();
}

void StructureViewPanel::setUseHexadecimalValues(bool enabled)
{
    if (m_useHexadecimalValues == enabled)
        return;

    m_useHexadecimalValues = enabled;
    applyDisplayOptions();
}

void StructureViewPanel::setUseHexadecimalOffsets(bool enabled)
{
    if (m_useHexadecimalOffsets == enabled)
        return;

    m_useHexadecimalOffsets = enabled;
    applyDisplayOptions();
}

void StructureViewPanel::setUseRelativeOffsets(bool enabled)
{
    if (m_useRelativeOffsets == enabled)
        return;

    m_useRelativeOffsets = enabled;
    applyDisplayOptions();
}

void StructureViewPanel::updateHexViewSelection(const QModelIndex &current)
{
    if (!m_hv || !m_model || m_rebuildingRows)
        return;

    StructureRow *row = m_model->rowForIndex(current);
    if (!row || row->parent == nullptr)
    {
        clearHexViewOverlay();
        return;
    }

    if (row->byteLength > 0 && row->absoluteOffset < m_hv->size())
    {
        const uint64_t requestedEnd = row->absoluteOffset > UINT64_MAX - row->byteLength
            ? UINT64_MAX
            : row->absoluteOffset + row->byteLength;
        const size_w start = static_cast<size_w>(row->absoluteOffset);
        const size_w end = static_cast<size_w>(qMin<uint64_t>(requestedEnd, m_hv->size()));
        if (end > start)
            setHexViewSelectionFromStructure(start, end);
    }

    StructureRow *scope = row->parent;
    while (scope && scope->parent && scope->byteLength == 0)
        scope = scope->parent;

    if (!scope || !scope->parent || scope->byteLength == 0 || scope->absoluteOffset >= m_hv->size())
    {
        clearHexViewOverlay();
        return;
    }

    HexView::OverlayRange range;
    range.offset = static_cast<size_w>(scope->absoluteOffset);
    range.length = static_cast<size_w>(qMin<uint64_t>(scope->byteLength, m_hv->size() - range.offset));
    if (range.length == 0)
    {
        clearHexViewOverlay();
        return;
    }
    range.bgSlot = HVC_RANGE_OVERLAY;
    range.priority = 1;
    m_hv->setOverlayRanges(HexView::OverlayLayer::StructureView, {range});
}

void StructureViewPanel::clearHexViewOverlay()
{
    if (m_hv)
        m_hv->clearOverlayRanges(HexView::OverlayLayer::StructureView);
}

void StructureViewPanel::setHexViewSelectionFromStructure(size_w start, size_w end)
{
    if (!m_hv)
        return;

    m_updatingHexViewFromStructure = true;
    m_hv->setCurSel(start, end, true);
    m_hv->scrollCenterIfOffScreen(start, end - start);
    m_updatingHexViewFromStructure = false;
}

bool StructureViewPanel::explicitRootOffset(TypeDecl *rootType, uint64_t *offset) const
{
    if (!rootType || !offset)
        return false;

    ExprNode *expr = nullptr;
    if (!FindTag(rootType->tagList, TOK_OFFSET, &expr) || !expr)
        return false;

    // Root-level offsets are a file-placement hint for exported definitions.
    // They must be constant here; data-dependent field offsets are handled by
    // StructureRenderEngine while it walks already-rendered rows.
    switch (expr->type)
    {
    case EXPR_NUMBER:
    case EXPR_UNARY:
    case EXPR_BINARY:
    case EXPR_TERTIARY:
        *offset = static_cast<uint64_t>(Evaluate(expr));
        return true;
    default:
        return false;
    }
}

bool StructureViewPanel::magicSignatureMatches(const StructureMagicSignature &signature) const
{
    if (!m_hv || signature.bytes.isEmpty())
        return false;

    QByteArray fileBytes(signature.bytes.size(), Qt::Uninitialized);
    const size_t bytesRead = m_hv->getData(static_cast<size_w>(signature.offset),
                                           reinterpret_cast<uint8_t *>(fileBytes.data()),
                                           static_cast<size_t>(fileBytes.size()));
    return bytesRead == static_cast<size_t>(fileBytes.size())
        && fileBytes == signature.bytes;
}

bool StructureViewPanel::selectAssociatedRootType(const QList<ExportedStructureType> &exportedTypes)
{
    if (!m_rootCombo)
        return false;

    const int index = associatedRootTypeIndex(exportedTypes);
    if (index < 0)
        return false;

    const bool changed = m_rootCombo->currentIndex() != index;
    m_rootCombo->setCurrentIndex(index);
    return changed;
}

int StructureViewPanel::associatedRootTypeIndex(const QList<ExportedStructureType> &exportedTypes) const
{
    if (!m_hv || exportedTypes.isEmpty())
        return -1;

    const QString fileName = QFileInfo(m_hv->filePath()).fileName().toLower();
    if (!fileName.isEmpty())
    {
        for (int i = 0; i < exportedTypes.size(); ++i)
        {
            for (QString ext : exportedTypes[i].assocExtensions)
            {
                ext = ext.trimmed().toLower();
                if (ext.isEmpty())
                    continue;
                if (!ext.startsWith(QLatin1Char('.')))
                    ext.prepend(QLatin1Char('.'));

                if (fileName.endsWith(ext) || fileName.contains(ext + QLatin1Char('.')))
                    return i;
            }
        }
    }

    for (int i = 0; i < exportedTypes.size(); ++i)
    {
        for (const StructureMagicSignature &signature : exportedTypes[i].magicSignatures)
        {
            if (magicSignatureMatches(signature))
                return i;
        }
    }

    return -1;
}

void StructureViewPanel::refreshForCurrentFileAssociation()
{
    if (!m_definitions || !m_rootCombo || !m_model)
        return;

    m_definitions->ensureLoaded();
    const QList<ExportedStructureType> exportedTypes = m_definitions->exportedTypes();
    const int associatedIndex = associatedRootTypeIndex(exportedTypes);
    if (associatedIndex < 0)
    {
        m_rootCombo->setCurrentIndex(-1);
        m_model->clear();
        clearHexViewOverlay();
        updateOffsetDisplay();
        return;
    }

    const bool rootChanged = m_rootCombo->currentIndex() != associatedIndex;
    m_rootCombo->setCurrentIndex(associatedIndex);
    updateOffsetDisplay();
    if (!rootChanged)
        rebuildRows();
}

TypeDecl *StructureViewPanel::selectedRootType() const
{
    if (!m_rootCombo || m_rootCombo->currentIndex() < 0)
        return nullptr;

    const qulonglong ptr = m_rootCombo->currentData().toULongLong();
    return reinterpret_cast<TypeDecl *>(ptr);
}

QString StructureViewPanel::displayNameForTypeDecl(TypeDecl *decl) const
{
    if (!decl)
        return {};

    for (Type *type : decl->declList)
    {
        if (type && type->sym)
            return QString::fromLocal8Bit(type->sym->name);
    }

    if (decl->baseType && decl->baseType->ty == typeSTRUCT && decl->baseType->sptr && decl->baseType->sptr->symbol)
        return tr("struct %1").arg(QString::fromLocal8Bit(decl->baseType->sptr->symbol->name));

    return tr("(anonymous type)");
}

bool StructureViewPanel::selectedRootHasLoadError(QString *filePath, QString *message) const
{
    if (!m_rootCombo || m_rootCombo->currentIndex() < 0)
        return false;

    const QString path = m_rootCombo->currentData(kRootComboFilePathRole).toString();
    if (path.isEmpty())
        return false;

    if (filePath)
        *filePath = path;
    if (message)
        *message = m_rootCombo->currentData(kRootComboErrorRole).toString();
    return true;
}

void StructureViewPanel::updateLoadErrorView()
{
    if (!m_loadErrorView)
        return;

    QString filePath;
    if (!selectedRootHasLoadError(&filePath, nullptr))
        return;

    m_loadErrorView->setText(
        tr("<div align='center'>Error loading %1<br><br><a href=\"#\">View source</a></div>")
            .arg(QFileInfo(filePath).fileName().toHtmlEscaped()));
}

StructureViewPanelHost::StructureViewPanelHost(HexView *hv, QWidget *parent)
    : SidePanelHostBase(450, 300, 800, true, parent)
    , m_hv(hv)
{
}

QWidget *StructureViewPanelHost::createPanelWidget()
{
    auto *panel = new StructureViewPanel(m_hv);
    connect(panel, &StructureViewPanel::closeRequested,
            this, &StructureViewPanelHost::closePanel);
    connect(panel, &StructureViewPanel::openDisassemblerRequested,
            this, &StructureViewPanelHost::openDisassemblerRequested);
    return panel;
}
