#include "CqlUtil.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimeZone>

#include <algorithm>
#include <cmath>


namespace {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
inline QTimeZone utcZone() { return QTimeZone(QTimeZone::UTC); }   // работает и без QT_CONFIG(timezone)
inline void setUtc(QDateTime &dt) { dt.setTimeZone(utcZone()); }
inline QDateTime startOfDayUtc(const QDate &d) { return d.startOfDay(utcZone()); }
#else
inline void setUtc(QDateTime &dt) { dt.setTimeSpec(Qt::UTC); }
inline QDateTime startOfDayUtc(const QDate &d) { return d.startOfDay(Qt::UTC); }
#endif
} // namespace


void sortColumns(QVector<ColumnInfo> &cols)
{
    auto rank = [](const ColumnInfo &c) {
        if (c.isPartitionKey()) return 0;
        if (c.isClustering()) return 1;
        if (c.isStatic()) return 2;
        return 3;
    };
    std::stable_sort(cols.begin(), cols.end(), [&](const ColumnInfo &a, const ColumnInfo &b) {
        const int ra = rank(a), rb = rank(b);
        if (ra != rb) return ra < rb;
        if (ra <= 1) return a.position < b.position;
        return a.name < b.name;
    });
}

namespace Cql
{

namespace
{
const QStringList &numericTypes()
{
    static const QStringList t = { "tinyint", "smallint", "int", "bigint", "varint",
                                   "float", "double", "decimal", "counter" };
    return t;
}

const QStringList &quotedTypes()
{
    static const QStringList t = { "text", "ascii", "varchar", "inet", "date", "time", "timestamp" };
    return t;
}

const QStringList &scalarTypes()
{
    static const QStringList t = { "ascii", "bigint", "blob", "boolean", "counter", "date",
                                   "decimal", "double", "duration", "float", "inet", "int",
                                   "smallint", "text", "time", "timestamp", "timeuuid",
                                   "tinyint", "uuid", "varchar", "varint" };
    return t;
}

void skipSpaces(const QString &s, int &i)
{
    while (i < s.size() && s.at(i).isSpace()) ++i;
}

Type parseAt(const QString &s, int &i)
{
    skipSpaces(s, i);
    Type t;
    QString name;
    if (i < s.size() && s.at(i) == '"') {               // "QuotedUdt"
        ++i;
        while (i < s.size()) {
            if (s.at(i) == '"') {
                if (i + 1 < s.size() && s.at(i + 1) == '"') { name += '"'; i += 2; continue; }
                ++i; break;
            }
            name += s.at(i++);
        }
        t.name = name;
    } else {
        while (i < s.size() && s.at(i) != '<' && s.at(i) != '>' && s.at(i) != ',')
            name += s.at(i++);
        t.name = name.trimmed().toLower();
    }
    skipSpaces(s, i);
    if (i < s.size() && s.at(i) == '<') {
        ++i;
        while (i < s.size()) {
            t.args << parseAt(s, i);
            skipSpaces(s, i);
            if (i < s.size() && s.at(i) == ',') { ++i; continue; }
            if (i < s.size() && s.at(i) == '>') { ++i; }
            break;
        }
    }
    if (t.name == QLatin1String("frozen") && t.args.size() == 1) {
        Type inner = t.args.first();
        inner.frozen = true;
        return inner;
    }
    return t;
}

QString numberToString(double d)
{
    if (std::floor(d) == d && std::fabs(d) < 1e15)
        return QString::number(qint64(d));
    return QString::number(d, 'g', 17);
}

QString normalizeTimestamp(const QString &in)
{
    const QString s = in.trimmed();
    static const QRegularExpression digits(QStringLiteral("^-?\\d+$"));
    if (digits.match(s).hasMatch())
        return s; // миллисекунды от эпохи — без кавычек

    QDateTime dt = QDateTime::fromString(s, Qt::ISODateWithMs);
    if (!dt.isValid()) {
        static const char *formats[] = { "yyyy-MM-dd HH:mm:ss.zzz", "yyyy-MM-dd HH:mm:ss",
                                         "yyyy-MM-dd HH:mm", "yyyy-MM-dd", "dd.MM.yyyy HH:mm:ss",
                                         "dd.MM.yyyy" };
        for (const char *f : formats) {
            dt = QDateTime::fromString(s, QString::fromLatin1(f));
            if (dt.isValid()) { 
            //dt.setTimeZone(QTimeZone::utc());
            setUtc(dt);
             break; }
        }
    }
    if (!dt.isValid())
        return quoteString(s); // пусть разбирает сервер
    return quoteString(dt.toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")) +
                       QStringLiteral("+0000"));
}

QString scalarLiteral(const QString &raw, const Type &t)
{
    const QString s = raw.trimmed();
    const QString &n = t.name;

    if (n == QLatin1String("text") || n == QLatin1String("varchar") || n == QLatin1String("ascii"))
        return quoteString(raw);
    if (s.isEmpty() || s.compare(QLatin1String("null"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("null");
    if (n == QLatin1String("timestamp"))
        return normalizeTimestamp(s);
    if (t.isTextual())
        return quoteString(s);
    if (n == QLatin1String("blob")) {
        if (s.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
            return s;
        return QStringLiteral("0x") + QString::fromLatin1(raw.toUtf8().toHex());
    }
    if (n == QLatin1String("boolean"))
        return s.toLower();
    if (n.isEmpty()) {
        bool ok = false;
        s.toDouble(&ok);
        if (ok || s == QLatin1String("true") || s == QLatin1String("false"))
            return s;
        return quoteString(raw);
    }
    // числа, uuid, timeuuid, duration и т.п. — как есть
    return s;
}

} // namespace

bool Type::isCollection() const
{
    if (name == QLatin1String("list") || name == QLatin1String("set") ||
        name == QLatin1String("map") || name == QLatin1String("tuple") ||
        name == QLatin1String("vector"))
        return true;
    return !name.isEmpty() && !scalarTypes().contains(name) && !name.startsWith('\'');
}

bool Type::isNumeric() const { return numericTypes().contains(name); }
bool Type::isTextual() const { return quotedTypes().contains(name); }

QString Type::toString() const
{
    QString s = name;
    if (!args.isEmpty()) {
        QStringList parts;
        for (const auto &a : args) parts << a.toString();
        s += QLatin1Char('<') + parts.join(QStringLiteral(", ")) + QLatin1Char('>');
    }
    return frozen ? QStringLiteral("frozen<%1>").arg(s) : s;
}

Type parseType(const QString &s)
{
    int i = 0;
    return parseAt(s, i);
}

QString quoteIdent(const QString &name)
{
    static const QRegularExpression plain(QStringLiteral("^[a-z][a-z0-9_]*$"));
    static const QStringList reserved = {
        "add", "allow", "alter", "and", "apply", "asc", "authorize", "batch", "begin", "by",
        "columnfamily", "create", "delete", "desc", "describe", "drop", "entries", "execute",
        "from", "full", "grant", "if", "in", "index", "infinity", "insert", "into", "is",
        "keyspace", "limit", "modify", "nan", "norecursive", "not", "null", "of", "on", "or",
        "order", "primary", "rename", "replace", "revoke", "schema", "select", "set", "table",
        "to", "token", "truncate", "unlogged", "update", "use", "using", "where", "with" };
    if (plain.match(name).hasMatch() && !reserved.contains(name))
        return name;
    QString q = name;
    q.replace('"', QStringLiteral("\"\""));
    return QLatin1Char('"') + q + QLatin1Char('"');
}

QString quoteString(const QString &s)
{
    QString q = s;
    q.replace('\'', QStringLiteral("''"));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}

QString qualified(const QString &keyspace, const QString &table)
{
    if (keyspace.isEmpty())
        return quoteIdent(table);
    return quoteIdent(keyspace) + QLatin1Char('.') + quoteIdent(table);
}

QString jsonToLiteral(const QJsonValue &v, const Type &t)
{
    if (v.isNull() || v.isUndefined())
        return QStringLiteral("null");

    const QString &n = t.name;

    if (v.isArray()) {
        const QJsonArray a = v.toArray();
        QStringList parts;
        if (n == QLatin1String("tuple")) {
            for (int i = 0; i < a.size(); ++i)
                parts << jsonToLiteral(a.at(i), t.args.value(i));
            return QLatin1Char('(') + parts.join(QStringLiteral(", ")) + QLatin1Char(')');
        }
        for (const auto &e : a)
            parts << jsonToLiteral(e, t.args.value(0));
        if (n == QLatin1String("set"))
            return QLatin1Char('{') + parts.join(QStringLiteral(", ")) + QLatin1Char('}');
        return QLatin1Char('[') + parts.join(QStringLiteral(", ")) + QLatin1Char(']');
    }

    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        QStringList parts;
        if (n == QLatin1String("map")) {
            const Type kt = t.args.value(0);
            const Type vt = t.args.value(1);
            for (auto it = o.begin(); it != o.end(); ++it) {
                QString key;
                if (kt.isCollection()) {
                    const QJsonDocument d = QJsonDocument::fromJson(it.key().toUtf8());
                    key = d.isArray() ? jsonToLiteral(d.array(), kt)
                        : d.isObject() ? jsonToLiteral(d.object(), kt)
                                       : scalarLiteral(it.key(), kt);
                } else {
                    key = scalarLiteral(it.key(), kt);
                }
                parts << key + QStringLiteral(": ") + jsonToLiteral(it.value(), vt);
            }
        } else { // UDT: имена полей — идентификаторы
            for (auto it = o.begin(); it != o.end(); ++it)
                parts << quoteIdent(it.key()) + QStringLiteral(": ") + jsonToLiteral(it.value(), Type{});
        }
        return QLatin1Char('{') + parts.join(QStringLiteral(", ")) + QLatin1Char('}');
    }

    if (v.isBool())
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isDouble())
        return numberToString(v.toDouble());

    // строка
    if (n.isEmpty())
        return quoteString(v.toString());
    return scalarLiteral(v.toString(), t);
}

QString textToLiteral(const QString &text, const QString &typeStr)
{
    const Type t = parseType(typeStr);
    if (t.isCollection()) {
        const QString s = text.trimmed();
        if (s.isEmpty() || s.compare(QLatin1String("null"), Qt::CaseInsensitive) == 0)
            return QStringLiteral("null");
        const QJsonDocument d = QJsonDocument::fromJson(s.toUtf8());
        if (d.isArray())  return jsonToLiteral(d.array(), t);
        if (d.isObject()) return jsonToLiteral(d.object(), t);
        return s; // считаем, что пользователь ввёл литерал CQL
    }
    return scalarLiteral(text, t);
}

QString displayValue(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined: return QStringLiteral("null");
    case QJsonValue::Bool:      return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double:    return numberToString(v.toDouble());
    case QJsonValue::String:    return v.toString();
    case QJsonValue::Array:
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    case QJsonValue::Object:
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    }
    return {};
}

QString prettyValue(const QJsonValue &v)
{
    if (v.isArray())
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Indented)).trimmed();
    if (v.isObject())
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Indented)).trimmed();
    return displayValue(v);
}

bool toNumber(const QJsonValue &v, double *out, bool *isDateTime)
{
    if (isDateTime) *isDateTime = false;
    if (v.isDouble()) { *out = v.toDouble(); return true; }
    if (v.isBool())   { *out = v.toBool() ? 1.0 : 0.0; return true; }
    if (!v.isString()) return false;

    const QString s = v.toString().trimmed();
    bool ok = false;
    const double d = s.toDouble(&ok);
    if (ok) { *out = d; return true; }

    QDateTime dt = QDateTime::fromString(s, Qt::ISODateWithMs);
    if (!dt.isValid()) {
        const QDate date = QDate::fromString(s, Qt::ISODate);
        if (date.isValid())
        //    dt = date.startOfDay(QTimeZone::utc());
        dt = startOfDayUtc(date);
    }
    if (dt.isValid()) {
        *out = double(dt.toMSecsSinceEpoch());
        if (isDateTime) *isDateTime = true;
        return true;
    }
    return false;
}

QStringList splitStatements(const QString &script)
{
    QStringList out;
    QString cur;
    enum State { Normal, Single, Double, LineComment, BlockComment } st = Normal;
    static const QRegularExpression applyBatch(QStringLiteral("APPLY\\s+BATCH\\s*$"),
                                               QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression beginBatch(QStringLiteral("^BEGIN\\s+(UNLOGGED\\s+|COUNTER\\s+)?BATCH"),
                                               QRegularExpression::CaseInsensitiveOption);

    for (int i = 0; i < script.size(); ++i) {
        const QChar c = script.at(i);
        const QChar nx = i + 1 < script.size() ? script.at(i + 1) : QChar();
        switch (st) {
        case Normal:
            if (c == '\'') st = Single;
            else if (c == '"') st = Double;
            else if ((c == '-' && nx == '-') || (c == '/' && nx == '/')) { st = LineComment; ++i; continue; }
            else if (c == '/' && nx == '*') { st = BlockComment; ++i; continue; }
            else if (c == ';') {
                const QString t = cur.trimmed();
                if (beginBatch.match(t).hasMatch() && !applyBatch.match(t).hasMatch()) {
                    cur += c;   // внутри BEGIN BATCH ... APPLY BATCH
                    continue;
                }
                if (!t.isEmpty()) out << t;
                cur.clear();
                continue;
            }
            cur += c;
            break;
        case Single:
            cur += c;
            if (c == '\'') st = Normal; // '' обрабатывается как выход+вход
            break;
        case Double:
            cur += c;
            if (c == '"') st = Normal;
            break;
        case LineComment:
            if (c == '\n') { st = Normal; cur += c; }
            break;
        case BlockComment:
            if (c == '*' && nx == '/') { st = Normal; ++i; cur += ' '; }
            break;
        }
    }
    const QString t = cur.trimmed();
    if (!t.isEmpty()) out << t;
    return out;
}

QStringList builtinTypes()
{
    return { "text", "int", "bigint", "uuid", "timeuuid", "timestamp", "date", "time",
             "boolean", "double", "float", "decimal", "varint", "smallint", "tinyint",
             "blob", "inet", "ascii", "duration", "counter",
             "list<text>", "list<int>", "set<text>", "set<int>",
             "map<text, text>", "map<text, int>", "frozen<list<text>>",
             "tuple<text, int>" };
}

QStringList keywords()
{
    return { "ADD", "AGGREGATE", "ALL", "ALLOW", "ALTER", "AND", "APPLY", "AS", "ASC",
             "BATCH", "BEGIN", "BY", "CLUSTERING", "COMPACT", "CONTAINS", "COUNT", "COUNTER",
             "CREATE", "CUSTOM", "DELETE", "DESC", "DESCRIBE", "DISTINCT", "DROP", "ENTRIES",
             "EXISTS", "FILTERING", "FROM", "FROZEN", "FUNCTION", "GRANT", "GROUP", "IF", "IN",
             "INDEX", "INSERT", "INTO", "IS", "JSON", "KEY", "KEYSPACE", "KEYSPACES", "LIMIT",
             "LIST", "LOGIN", "MAP", "MATERIALIZED", "MODIFY", "NOT", "NULL", "OF", "ON", "OR",
             "ORDER", "PARTITION", "PASSWORD", "PER", "PERMISSIONS", "PRIMARY", "RENAME",
             "REPLACE", "REPLICATION", "REVOKE", "ROLE", "ROLES", "SELECT", "SET", "STATIC",
             "STORAGE", "TABLE", "TABLES", "TIMESTAMP", "TO", "TOKEN", "TRUNCATE", "TTL",
             "TUPLE", "TYPE", "UNLOGGED", "UPDATE", "USE", "USER", "USERS", "USING", "VALUES",
             "VIEW", "WHERE", "WITH", "WRITETIME", "TRUE", "FALSE" };
}

} // namespace Cql
