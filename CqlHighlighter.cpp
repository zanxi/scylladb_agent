#include "CqlHighlighter.h"
#include "CqlUtil.h"

CqlHighlighter::CqlHighlighter(QTextDocument *doc)
    : QSyntaxHighlighter(doc)
{
    QTextCharFormat kw;
    kw.setForeground(QColor(0x1f, 0x5f, 0xbf));
    kw.setFontWeight(QFont::Bold);
    m_rules.append({ QRegularExpression(QStringLiteral("\\b(%1)\\b").arg(Cql::keywords().join('|')),
                                        QRegularExpression::CaseInsensitiveOption), kw });

    QTextCharFormat types;
    types.setForeground(QColor(0x8a, 0x3f, 0xb0));
    m_rules.append({ QRegularExpression(QStringLiteral(
                         "\\b(text|varchar|ascii|int|bigint|smallint|tinyint|varint|uuid|timeuuid|"
                         "timestamp|date|time|duration|boolean|double|float|decimal|blob|inet|"
                         "counter|frozen|list|set|map|tuple)\\b"),
                                        QRegularExpression::CaseInsensitiveOption), types });

    QTextCharFormat num;
    num.setForeground(QColor(0xb0, 0x5a, 0x00));
    m_rules.append({ QRegularExpression(QStringLiteral("\\b-?\\d+(\\.\\d+)?([eE][-+]?\\d+)?\\b")), num });

    QTextCharFormat str;
    str.setForeground(QColor(0x2e, 0x8b, 0x3e));
    m_rules.append({ QRegularExpression(QStringLiteral("'([^']|'')*'")), str });

    QTextCharFormat ident;
    ident.setForeground(QColor(0x99, 0x33, 0x33));
    m_rules.append({ QRegularExpression(QStringLiteral("\"([^\"]|\"\")*\"")), ident });

    m_commentFmt.setForeground(Qt::gray);
    m_commentFmt.setFontItalic(true);
    m_rules.append({ QRegularExpression(QStringLiteral("(--|//)[^\n]*")), m_commentFmt });
}

void CqlHighlighter::highlightBlock(const QString &text)
{
    for (const Rule &r : std::as_const(m_rules)) {
        auto it = r.re.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            setFormat(int(m.capturedStart()), int(m.capturedLength()), r.fmt);
        }
    }

    // Многострочные комментарии /* ... */
    setCurrentBlockState(0);
    qsizetype start = 0;
    if (previousBlockState() != 1) {
        const auto m = m_blockStart.match(text);
        start = m.hasMatch() ? m.capturedStart() : -1;
    }
    while (start >= 0) {
        const auto end = m_blockEnd.match(text, start);
        qsizetype len;
        if (!end.hasMatch()) {
            setCurrentBlockState(1);
            len = text.size() - start;
        } else {
            len = end.capturedEnd() - start;
        }
        setFormat(int(start), int(len), m_commentFmt);
        const auto next = m_blockStart.match(text, start + len);
        start = next.hasMatch() ? next.capturedStart() : -1;
    }
}
