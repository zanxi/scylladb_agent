#include "LogWidget.h"

#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextStream>
#include <QVBoxLayout>

namespace
{
constexpr int kMaxEntries = 20000;

QString colorFor(const QString &level)
{
    if (level == QLatin1String("ERROR")) return QStringLiteral("#c62828");
    if (level == QLatin1String("WARN"))  return QStringLiteral("#ef6c00");
    if (level == QLatin1String("OK"))    return QStringLiteral("#2e7d32");
    if (level == QLatin1String("CQL"))   return QStringLiteral("#1565c0");
    return QStringLiteral("#555555");
}
} // namespace

LogWidget::LogWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(2, 2, 2, 2);

    auto *bar = new QHBoxLayout;
    bar->addWidget(new QLabel(tr("Показывать:")));
    m_levelFilter = new QComboBox;
    m_levelFilter->addItems({ tr("Все"), tr("Только CQL"), tr("Ошибки и предупреждения"), tr("Только ошибки") });
    bar->addWidget(m_levelFilter);
    bar->addStretch();
    auto *saveBtn = new QPushButton(tr("Сохранить…"));
    auto *clearBtn = new QPushButton(tr("Очистить"));
    bar->addWidget(saveBtn);
    bar->addWidget(clearBtn);
    lay->addLayout(bar);

    m_view = new QPlainTextEdit;
    m_view->setReadOnly(true);
    m_view->setMaximumBlockCount(kMaxEntries);
    m_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    lay->addWidget(m_view);

    connect(m_levelFilter, &QComboBox::currentIndexChanged, this, &LogWidget::rebuild);
    connect(saveBtn, &QPushButton::clicked, this, &LogWidget::save);
    connect(clearBtn, &QPushButton::clicked, this, &LogWidget::clear);
}

bool LogWidget::passes(const QString &level) const
{
    switch (m_levelFilter->currentIndex()) {
    case 1: return level == QLatin1String("CQL");
    case 2: return level == QLatin1String("ERROR") || level == QLatin1String("WARN");
    case 3: return level == QLatin1String("ERROR");
    default: return true;
    }
}

QString LogWidget::toHtml(const Entry &e) const
{
    QString msg = e.message.toHtmlEscaped();
    msg.replace('\n', QStringLiteral("<br>&nbsp;&nbsp;&nbsp;&nbsp;"));
    return QStringLiteral("<span style='color:#888'>%1</span> "
                          "<b style='color:%2'>%3</b> <span style='color:%2'>%4</span>")
        .arg(e.time, colorFor(e.level), e.level.leftJustified(5, ' ').replace(' ', "&nbsp;"), msg);
}

void LogWidget::append(const QString &level, const QString &message)
{
    Entry e{ QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz")), level, message };
    m_entries.append(e);
    if (m_entries.size() > kMaxEntries)
        m_entries.removeFirst();

    if (!passes(level))
        return;
    QScrollBar *sb = m_view->verticalScrollBar();
    const bool atBottom = sb->value() >= sb->maximum() - 4;
    m_view->appendHtml(toHtml(e));
    if (atBottom)
        sb->setValue(sb->maximum());
}

void LogWidget::rebuild()
{
    m_view->clear();
    for (const Entry &e : std::as_const(m_entries))
        if (passes(e.level))
            m_view->appendHtml(toHtml(e));
}

void LogWidget::clear()
{
    m_entries.clear();
    m_view->clear();
}

void LogWidget::save()
{
    const QString file = QFileDialog::getSaveFileName(
        this, tr("Сохранить лог"),
        QStringLiteral("scylla_client_%1.log").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")),
        tr("Лог (*.log *.txt)"));
    if (file.isEmpty())
        return;
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Ошибка"), f.errorString());
        return;
    }
    QTextStream out(&f);
    for (const Entry &e : std::as_const(m_entries))
        out << e.time << ' ' << e.level.leftJustified(5) << ' ' << e.message << '\n';
}
