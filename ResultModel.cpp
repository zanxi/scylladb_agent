#include "ResultModel.h"

#include <QApplication>
#include <QBrush>
#include <QFont>
#include <QPalette>
#include <QRegularExpression>

namespace
{
constexpr int kMaxCellChars = 300;
}

ResultModel::ResultModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void ResultModel::setResult(const QVector<ColumnInfo> &columns, const QJsonArray &rows,
                            const QString &keyspace, const QString &table, bool editable)
{
    beginResetModel();
    m_columns = columns;
    m_keyspace = keyspace;
    m_table = table;
    m_editable = editable;
    m_rows.clear();
    m_rows.reserve(rows.size());
    for (const auto &rv : rows) {
        const QJsonArray r = rv.toArray();
        QVector<QJsonValue> row(m_columns.size(), QJsonValue(QJsonValue::Null));
        for (int c = 0; c < m_columns.size() && c < r.size(); ++c)
            row[c] = r.at(c);
        m_rows << row;
    }
    endResetModel();
}

void ResultModel::clear()
{
    beginResetModel();
    m_columns.clear();
    m_rows.clear();
    m_keyspace.clear();
    m_table.clear();
    m_editable = false;
    endResetModel();
}

int ResultModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int ResultModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_columns.size());
}

QVariant ResultModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size() || index.column() >= m_columns.size())
        return {};

    const QJsonValue &v = m_rows.at(index.row()).at(index.column());
    const ColumnInfo &col = m_columns.at(index.column());

    switch (role) {
    case Qt::DisplayRole: {
        QString s = Cql::displayValue(v);
        s.replace('\n', QChar(0x21B5)); // ↵
        if (s.size() > kMaxCellChars)
            s = s.left(kMaxCellChars) + QChar(0x2026);
        return s;
    }
    case Qt::EditRole:
        return v.isNull() ? QString() : Cql::displayValue(v);
    case RawTextRole:
        return Cql::displayValue(v);
    case FullTextRole:
        return Cql::prettyValue(v);
    case TypeRole:
        return col.type;
    case IsNullRole:
        return v.isNull();
    case SortRole: {
        if (v.isNull()) return {};
        double d = 0;
        if (Cql::Type t = Cql::parseType(col.type); t.isNumeric() && Cql::toNumber(v, &d))
            return d;
        if (v.isDouble()) return v.toDouble();
        return Cql::displayValue(v);
    }
    case Qt::ForegroundRole:
        if (v.isNull())
            return QApplication::palette().brush(QPalette::Disabled, QPalette::Text);
        return {};
    case Qt::FontRole:
        if (v.isNull()) { QFont f; f.setItalic(true); return f; }
        return {};
    case Qt::TextAlignmentRole:
        if (Cql::parseType(col.type).isNumeric())
            return int(Qt::AlignRight | Qt::AlignVCenter);
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    default:
        return {};
    }
}

QVariant ResultModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o == Qt::Vertical) {
        if (role == Qt::DisplayRole) return section + 1;
        return {};
    }
    if (section < 0 || section >= m_columns.size())
        return {};
    const ColumnInfo &c = m_columns.at(section);
    switch (role) {
    case Qt::DisplayRole:
        if (c.isPartitionKey()) return QStringLiteral("\U0001F511 %1").arg(c.name);
        if (c.isClustering())   return QStringLiteral("↕ %1").arg(c.name);
        return c.name;
    case Qt::ToolTipRole: {
        QString kind = c.kind.isEmpty() ? tr("вычисляемая") : c.kind;
        return QStringLiteral("<b>%1</b><br>тип: %2<br>вид: %3").arg(c.name.toHtmlEscaped(),
                                                                    c.type.toHtmlEscaped(), kind);
    }
    case Qt::FontRole:
        if (c.isPrimaryKey()) { QFont f; f.setBold(true); return f; }
        return {};
    default:
        return {};
    }
}

Qt::ItemFlags ResultModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (!index.isValid() || !m_editable)
        return f;
    const ColumnInfo &c = m_columns.at(index.column());
    if (!c.isPrimaryKey() && !c.kind.isEmpty() && c.type != QLatin1String("counter"))
        f |= Qt::ItemIsEditable;
    return f;
}

bool ResultModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !(flags(index) & Qt::ItemIsEditable))
        return false;
    const QString text = value.toString();
    if (text == data(index, Qt::EditRole).toString())
        return false;
    emit editRequested(index.row(), index.column(), text);
    return false; // значение обновится после подтверждения сервером
}

int ResultModel::columnIndex(const QString &name) const
{
    for (int i = 0; i < m_columns.size(); ++i)
        if (m_columns.at(i).name == name) return i;
    return -1;
}

QJsonValue ResultModel::value(int row, int col) const
{
    if (row < 0 || row >= m_rows.size() || col < 0 || col >= m_columns.size())
        return QJsonValue(QJsonValue::Undefined);
    return m_rows.at(row).at(col);
}

QJsonArray ResultModel::rowValues(int row) const
{
    QJsonArray a;
    if (row < 0 || row >= m_rows.size()) return a;
    for (const auto &v : m_rows.at(row)) a.append(v);
    return a;
}

void ResultModel::setRowValues(int row, const QJsonArray &values)
{
    if (row < 0 || row >= m_rows.size()) return;
    for (int c = 0; c < m_columns.size() && c < values.size(); ++c)
        m_rows[row][c] = values.at(c);
    emit dataChanged(index(row, 0), index(row, int(m_columns.size()) - 1));
}

void ResultModel::removeRowAt(int row)
{
    if (row < 0 || row >= m_rows.size()) return;
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
}

QString ResultModel::whereClause(int row) const
{
    if (row < 0 || row >= m_rows.size()) return {};
    QStringList parts;
    for (int c = 0; c < m_columns.size(); ++c) {
        const ColumnInfo &col = m_columns.at(c);
        if (!col.isPrimaryKey()) continue;
        const QJsonValue &v = m_rows.at(row).at(c);
        if (v.isNull()) return {};
        parts << QStringLiteral("%1 = %2").arg(Cql::quoteIdent(col.name),
                                               Cql::jsonToLiteral(v, Cql::parseType(col.type)));
    }
    return parts.join(QStringLiteral(" AND "));
}

QString ResultModel::insertStatement(int row) const
{
    if (row < 0 || row >= m_rows.size() || m_table.isEmpty()) return {};
    QStringList names, values;
    for (int c = 0; c < m_columns.size(); ++c) {
        const ColumnInfo &col = m_columns.at(c);
        const QJsonValue &v = m_rows.at(row).at(c);
        if (col.kind.isEmpty() || v.isNull() || col.type == QLatin1String("counter")) continue;
        names << Cql::quoteIdent(col.name);
        values << Cql::jsonToLiteral(v, Cql::parseType(col.type));
    }
    return QStringLiteral("INSERT INTO %1 (%2) VALUES (%3);")
        .arg(Cql::qualified(m_keyspace, m_table), names.join(", "), values.join(", "));
}

// ---------------------------------------------------------------------------

ValueFilterProxy::ValueFilterProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setFilterRole(ResultModel::RawTextRole);
    setSortRole(ResultModel::SortRole);
    setFilterKeyColumn(-1);
}

void ValueFilterProxy::setSearch(const QString &text, int column, Mode mode, bool caseSensitive)
{
    setFilterKeyColumn(column);
    if (text.isEmpty()) {
        setFilterRegularExpression(QRegularExpression());
        return;
    }
    QString pattern;
    switch (mode) {
    case Contains: pattern = QRegularExpression::escape(text); break;
    case Equals:   pattern = QStringLiteral("^%1$").arg(QRegularExpression::escape(text)); break;
    case Regex:    pattern = text; break;
    }
    QRegularExpression re(pattern, caseSensitive ? QRegularExpression::NoPatternOption
                                                 : QRegularExpression::CaseInsensitiveOption);
    if (!re.isValid())
        return; // недописанное регулярное выражение — оставляем прежний фильтр
    setFilterRegularExpression(re);
}

bool ValueFilterProxy::lessThan(const QModelIndex &l, const QModelIndex &r) const
{
    const QVariant a = l.data(sortRole());
    const QVariant b = r.data(sortRole());
    if (!a.isValid()) return b.isValid();
    if (!b.isValid()) return false;
    if (a.typeId() == QMetaType::Double && b.typeId() == QMetaType::Double)
        return a.toDouble() < b.toDouble();
    return QString::localeAwareCompare(a.toString(), b.toString()) < 0;
}
