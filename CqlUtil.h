#pragma once

#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

// Описание колонки результата / таблицы.
struct ColumnInfo
{
    QString name;
    QString type;
    QString kind;           // partition_key | clustering | regular | static | "" (неизвестно)
    int     position = -1;
    QString clusteringOrder;

    bool isPartitionKey() const { return kind == QLatin1String("partition_key"); }
    bool isClustering() const   { return kind == QLatin1String("clustering"); }
    bool isPrimaryKey() const   { return isPartitionKey() || isClustering(); }
    bool isStatic() const       { return kind == QLatin1String("static"); }
};

// Сортировка: partition key, clustering по позиции, затем остальные по имени.
void sortColumns(QVector<ColumnInfo> &cols);

namespace Cql
{
// Разобранный тип CQL: map<text, frozen<list<int>>> -> {map, [text, {list,[int]}]}
struct Type
{
    QString        name;    // в нижнем регистре; для UDT — имя типа
    QVector<Type>  args;
    bool           frozen = false;

    bool isCollection() const;
    bool isNumeric() const;
    bool isTextual() const;   // значение записывается в кавычках
    QString toString() const;
};

Type parseType(const QString &s);

QString quoteIdent(const QString &name);
QString quoteString(const QString &s);
QString qualified(const QString &keyspace, const QString &table);

// JSON-значение (как прислал сервер) -> литерал CQL для данного типа.
QString jsonToLiteral(const QJsonValue &v, const Type &type);

// Текст, введённый пользователем -> литерал CQL. Для коллекций принимается
// как JSON (["a","b"], {"k":1}), так и готовый литерал CQL.
QString textToLiteral(const QString &text, const QString &type);

// Значение для отображения в таблице.
QString displayValue(const QJsonValue &v);
// Развёрнутое значение для всплывающего окна (JSON с отступами).
QString prettyValue(const QJsonValue &v);

// Число для графика: числа, булевы, числовые строки, даты/время (мс от эпохи).
bool toNumber(const QJsonValue &v, double *out, bool *isDateTime = nullptr);

// Разделение скрипта на операторы по ';' с учётом строк и комментариев.
QStringList splitStatements(const QString &script);

QStringList builtinTypes();
QStringList keywords();
} // namespace Cql
