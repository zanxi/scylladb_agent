#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

class CqlHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit CqlHighlighter(QTextDocument *doc);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule { QRegularExpression re; QTextCharFormat fmt; };
    QVector<Rule>      m_rules;
    QTextCharFormat    m_commentFmt;
    QRegularExpression m_blockStart{ QStringLiteral("/\\*") };
    QRegularExpression m_blockEnd{ QStringLiteral("\\*/") };
};
