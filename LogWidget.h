#pragma once

#include <QWidget>

class QComboBox;
class QPlainTextEdit;

// Журнал операций: время, уровень, сообщение. Фильтр по уровню, сохранение в файл.
class LogWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LogWidget(QWidget *parent = nullptr);

public slots:
    // level: INFO | OK | WARN | ERROR | CQL
    void append(const QString &level, const QString &message);
    void clear();
    void save();

private:
    struct Entry { QString time, level, message; };
    void rebuild();
    bool passes(const QString &level) const;
    QString toHtml(const Entry &e) const;

    QPlainTextEdit *m_view;
    QComboBox      *m_levelFilter;
    QList<Entry>    m_entries;
};
