#include "SchemaModel.h"

#include <QApplication>
#include <QBrush>
#include <QFont>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPalette>
#include <QStyle>

SchemaModel::SchemaModel(QObject *parent)
    : QStandardItemModel(parent)
{
    setHorizontalHeaderLabels({ tr("Объект") });
}

bool SchemaModel::isSystemKeyspace(const QString &ks)
{
    return ks.startsWith(QLatin1String("system")) || ks == QLatin1String("audit");
}

void SchemaModel::load(const QJsonObject &schema)
{
    clear();
    setHorizontalHeaderLabels({ tr("Объект") });
    m_tables.clear();
    m_views.clear();

    QStyle *st = QApplication::style();
    const QIcon ksIcon   = st->standardIcon(QStyle::SP_DriveHDIcon);
    const QIcon tblIcon  = st->standardIcon(QStyle::SP_FileDialogDetailedView);
    const QIcon viewIcon = st->standardIcon(QStyle::SP_FileDialogContentsView);
    const QIcon colIcon  = st->standardIcon(QStyle::SP_FileIcon);
    const QBrush grey    = QApplication::palette().brush(QPalette::Disabled, QPalette::Text);

    for (const auto &kv : schema.value("keyspaces").toArray()) {
        const QJsonObject ko = kv.toObject();
        const QString ks = ko.value("name").toString();

        auto *ksItem = new QStandardItem(ksIcon, ks);
        ksItem->setEditable(false);
        ksItem->setData(KeyspaceNode, NodeTypeRole);
        ksItem->setData(ks, KeyspaceRole);
        ksItem->setData(ks, NameRole);
        ksItem->setToolTip(tr("replication: %1")
                               .arg(QString::fromUtf8(QJsonDocument(ko.value("replication").toObject())
                                                          .toJson(QJsonDocument::Compact))));
        if (isSystemKeyspace(ks))
            ksItem->setForeground(grey);

        for (const auto &tv : ko.value("tables").toArray()) {
            const QJsonObject to = tv.toObject();
            const QString table = to.value("name").toString();
            const bool isView = to.value("view").toBool();

            QVector<ColumnInfo> cols;
            for (const auto &cv : to.value("columns").toArray()) {
                const QJsonObject co = cv.toObject();
                ColumnInfo c;
                c.name = co.value("name").toString();
                c.type = co.value("type").toString();
                c.kind = co.value("kind").toString();
                c.position = co.value("position").toInt(-1);
                c.clusteringOrder = co.value("clustering_order").toString();
                cols << c;
            }
            sortColumns(cols);
            m_tables.insert(ks + QLatin1Char('.') + table, cols);
            if (isView)
                m_views.insert(ks + QLatin1Char('.') + table);

            auto *tItem = new QStandardItem(isView ? viewIcon : tblIcon, table);
            tItem->setEditable(false);
            tItem->setData(isView ? ViewNode : TableNode, NodeTypeRole);
            tItem->setData(ks, KeyspaceRole);
            tItem->setData(table, TableRole);
            tItem->setData(table, NameRole);
            tItem->setToolTip(isView ? tr("Materialized view на %1")
                                           .arg(to.value("base_table").toString())
                                     : tr("Таблица %1.%2, колонок: %3").arg(ks, table).arg(cols.size()));

            for (const ColumnInfo &c : cols) {
                QString mark;
                if (c.isPartitionKey()) mark = QStringLiteral(" [PK]");
                else if (c.isClustering()) mark = QStringLiteral(" [CK %1]").arg(c.clusteringOrder.toUpper());
                else if (c.isStatic()) mark = QStringLiteral(" [static]");

                auto *cItem = new QStandardItem(colIcon, QStringLiteral("%1 : %2%3").arg(c.name, c.type, mark));
                cItem->setEditable(false);
                cItem->setData(ColumnNode, NodeTypeRole);
                cItem->setData(ks, KeyspaceRole);
                cItem->setData(table, TableRole);
                cItem->setData(c.name, ColumnRole);
                cItem->setData(c.name, NameRole);
                cItem->setData(c.type, TypeRole);
                cItem->setData(c.kind, KindRole);
                if (c.isPrimaryKey()) {
                    QFont f; f.setBold(true);
                    cItem->setFont(f);
                }
                tItem->appendRow(cItem);
            }
            ksItem->appendRow(tItem);
        }
        appendRow(ksItem);
    }
}

bool SchemaModel::hasTable(const QString &ks, const QString &table) const
{
    return m_tables.contains(ks + QLatin1Char('.') + table);
}

QVector<ColumnInfo> SchemaModel::columns(const QString &ks, const QString &table) const
{
    return m_tables.value(ks + QLatin1Char('.') + table);
}

bool SchemaModel::isView(const QString &ks, const QString &table) const
{
    return m_views.contains(ks + QLatin1Char('.') + table);
}

QStringList SchemaModel::keyspaces() const
{
    QStringList out;
    for (int i = 0; i < rowCount(); ++i)
        out << item(i)->data(KeyspaceRole).toString();
    return out;
}

// ---------------------------------------------------------------------------

SchemaFilterProxy::SchemaFilterProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setRecursiveFilteringEnabled(true);
    setAutoAcceptChildRows(true);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setFilterRole(SchemaModel::NameRole);
    setSortCaseSensitivity(Qt::CaseInsensitive);
}

void SchemaFilterProxy::setHideSystem(bool hide)
{
    m_hideSystem = hide;
    invalidateFilter();
}

bool SchemaFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    if (m_hideSystem &&
        SchemaModel::isSystemKeyspace(idx.data(SchemaModel::KeyspaceRole).toString()))
        return false;
    return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
}
