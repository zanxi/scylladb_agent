#pragma once

#include "CqlUtil.h"

#include <QAbstractTableModel>
#include <QJsonArray>
#include <QJsonValue>
#include <QSortFilterProxyModel>
#include <QVector>

// Результат запроса. Если привязан к таблице и содержит все колонки
// первичного ключа — ячейки (кроме ключевых и counter) редактируемы.
class ResultModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Roles {
        FullTextRole = Qt::UserRole + 100, // полное значение (для всплывающего окна)
        RawTextRole,                       // неусечённый текст (для поиска)
        SortRole,
        TypeRole,
        IsNullRole
    };

    explicit ResultModel(QObject *parent = nullptr);

    void setResult(const QVector<ColumnInfo> &columns, const QJsonArray &rows,
                   const QString &keyspace, const QString &table, bool editable);
    void clear();

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    const QVector<ColumnInfo> &columns() const { return m_columns; }
    const ColumnInfo &column(int c) const { return m_columns.at(c); }
    int columnIndex(const QString &name) const;
    QJsonValue value(int row, int col) const;
    QJsonArray rowValues(int row) const;
    void setRowValues(int row, const QJsonArray &values);
    void removeRowAt(int row);

    bool isEditable() const { return m_editable; }
    bool isBound() const { return !m_table.isEmpty(); }
    QString keyspace() const { return m_keyspace; }
    QString table() const { return m_table; }

    // WHERE pk1 = .. AND ck1 = .. для строки; пусто, если ключ неполный.
    QString whereClause(int row) const;
    // INSERT INTO ... для строки (копирование).
    QString insertStatement(int row) const;

signals:
    // Пользователь изменил ячейку; применение — забота вызывающей стороны.
    void editRequested(int row, int column, const QString &text);

private:
    QVector<ColumnInfo>         m_columns;
    QVector<QVector<QJsonValue>> m_rows;
    QString                     m_keyspace;
    QString                     m_table;
    bool                        m_editable = false;
};

// Поиск по значению: все колонки или одна, режимы «содержит/равно/regex».
class ValueFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    enum Mode { Contains, Equals, Regex };
    explicit ValueFilterProxy(QObject *parent = nullptr);

    void setSearch(const QString &text, int column, Mode mode, bool caseSensitive);

protected:
    bool lessThan(const QModelIndex &l, const QModelIndex &r) const override;
};
