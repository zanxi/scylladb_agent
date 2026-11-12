#pragma once

#include "CqlUtil.h"

#include <QDialog>
#include <QJsonArray>
#include <QVector>

class QChartView;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;

// Построение графика по выбранным колонкам. Диалог работает со снимком
// данных (строки, видимые после фильтра поиска), поэтому не зависит
// от последующих изменений таблицы в главном окне.
class ChartDialog : public QDialog
{
    Q_OBJECT
public:
    ChartDialog(const QVector<ColumnInfo> &columns, const QVector<QJsonArray> &rows,
                const QList<int> &preselectedY, const QString &title, QWidget *parent = nullptr);

private slots:
    void build();
    void savePng();

private:
    QJsonValue value(int row, int col) const { return m_rows.at(row).at(col); }

    QVector<ColumnInfo>    m_columns;
    QVector<QJsonArray>    m_rows;
    QComboBox             *m_x;
    QListWidget           *m_y;
    QComboBox             *m_kind;
    QCheckBox             *m_sortX;
    QLabel                *m_info;
    QChartView            *m_view;
};
