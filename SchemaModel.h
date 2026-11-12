#pragma once

#include "CqlUtil.h"

#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>

// Дерево: keyspace -> table/view -> column.
class SchemaModel : public QStandardItemModel
{
    Q_OBJECT
public:
    enum NodeType { KeyspaceNode = 1, TableNode, ViewNode, ColumnNode };
    enum Roles {
        NodeTypeRole = Qt::UserRole + 1,
        KeyspaceRole,
        TableRole,
        ColumnRole,
        TypeRole,
        KindRole,
        NameRole
    };

    explicit SchemaModel(QObject *parent = nullptr);

    void load(const QJsonObject &schema);

    bool hasTable(const QString &ks, const QString &table) const;
    QVector<ColumnInfo> columns(const QString &ks, const QString &table) const;
    bool isView(const QString &ks, const QString &table) const;
    QStringList keyspaces() const;
    static bool isSystemKeyspace(const QString &ks);

private:
    QHash<QString, QVector<ColumnInfo>> m_tables; // "ks.table" -> колонки
    QSet<QString>                       m_views;
};

// Поиск по названию на всех уровнях дерева + скрытие системных keyspace.
class SchemaFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit SchemaFilterProxy(QObject *parent = nullptr);
    void setHideSystem(bool hide);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    bool m_hideSystem = false;
};
