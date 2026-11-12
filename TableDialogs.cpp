#include "TableDialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace
{
const QRegularExpression &identRe()
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z][A-Za-z0-9_]*$"));
    return re;
}

QComboBox *typeCombo(const QString &current, QWidget *parent = nullptr)
{
    auto *cb = new QComboBox(parent);
    cb->setEditable(true);
    cb->addItems(Cql::builtinTypes());
    cb->setCurrentText(current);
    return cb;
}
} // namespace

// ---------------------------------------------------------------------------
CqlFormDialog::CqlFormDialog(const QString &title, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    auto *lay = new QVBoxLayout(this);
    m_body = new QVBoxLayout;
    lay->addLayout(m_body);
}

void CqlFormDialog::finishLayout()
{
    auto *lay = static_cast<QVBoxLayout *>(layout());

    lay->addWidget(new QLabel(tr("Будет выполнено:")));
    m_preview = new QPlainTextEdit;
    m_preview->setReadOnly(true);
    m_preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_preview->setMaximumHeight(110);
    lay->addWidget(m_preview);

    m_error = new QLabel;
    m_error->setStyleSheet(QStringLiteral("color:#c62828"));
    m_error->setWordWrap(true);
    lay->addWidget(m_error);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    m_ok->setText(tr("Выполнить"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);

    updatePreview();
}

void CqlFormDialog::updatePreview()
{
    if (!m_preview) return;
    const QString err = validate();
    m_error->setText(err);
    m_error->setVisible(!err.isEmpty());
    m_ok->setEnabled(err.isEmpty());
    m_preview->setPlainText(err.isEmpty() ? statement() : QString());
}

// ---------------------------------------------------------------------------
AddColumnDialog::AddColumnDialog(const QString &ks, const QString &table, QWidget *parent)
    : CqlFormDialog(tr("Добавить колонку в %1.%2").arg(ks, table), parent)
    , m_ks(ks), m_table(table)
{
    auto *form = new QFormLayout;
    m_name = new QLineEdit;
    m_type = typeCombo(QStringLiteral("text"));
    m_static = new QCheckBox(tr("STATIC (одно значение на партицию)"));
    form->addRow(tr("Имя:"), m_name);
    form->addRow(tr("Тип:"), m_type);
    form->addRow(QString(), m_static);
    m_body->addLayout(form);

    connect(m_name, &QLineEdit::textChanged, this, &AddColumnDialog::updatePreview);
    connect(m_type, &QComboBox::currentTextChanged, this, &AddColumnDialog::updatePreview);
    connect(m_static, &QCheckBox::toggled, this, &AddColumnDialog::updatePreview);
    finishLayout();
    resize(460, height());
}

QString AddColumnDialog::validate() const
{
    if (m_name->text().trimmed().isEmpty()) return tr("Укажите имя колонки");
    if (m_type->currentText().trimmed().isEmpty()) return tr("Укажите тип");
    return {};
}

QString AddColumnDialog::statement() const
{
    return QStringLiteral("ALTER TABLE %1 ADD %2 %3%4;")
        .arg(Cql::qualified(m_ks, m_table), Cql::quoteIdent(m_name->text().trimmed()),
             m_type->currentText().trimmed(), m_static->isChecked() ? QStringLiteral(" STATIC") : QString());
}

// ---------------------------------------------------------------------------
RenameColumnDialog::RenameColumnDialog(const QString &ks, const QString &table,
                                       const QString &column, QWidget *parent)
    : CqlFormDialog(tr("Переименовать колонку"), parent)
    , m_ks(ks), m_table(table), m_column(column)
{
    auto *form = new QFormLayout;
    m_newName = new QLineEdit(column);
    form->addRow(tr("Колонка:"), new QLabel(column));
    form->addRow(tr("Новое имя:"), m_newName);
    m_body->addLayout(form);
    auto *note = new QLabel(tr("<small>ScyllaDB позволяет переименовывать только колонки первичного ключа.</small>"));
    note->setWordWrap(true);
    m_body->addWidget(note);
    connect(m_newName, &QLineEdit::textChanged, this, &RenameColumnDialog::updatePreview);
    finishLayout();
}

QString RenameColumnDialog::statement() const
{
    return QStringLiteral("ALTER TABLE %1 RENAME %2 TO %3;")
        .arg(Cql::qualified(m_ks, m_table), Cql::quoteIdent(m_column),
             Cql::quoteIdent(m_newName->text().trimmed()));
}

// ---------------------------------------------------------------------------
InsertRowDialog::InsertRowDialog(const QString &ks, const QString &table,
                                 const QVector<ColumnInfo> &columns, const QJsonArray &prefill,
                                 QWidget *parent)
    : CqlFormDialog(tr("Новая строка в %1.%2").arg(ks, table), parent)
    , m_ks(ks), m_table(table)
{
    for (const ColumnInfo &c : columns)
        if (c.type != QLatin1String("counter"))
            m_columns << c;

    auto *note = new QLabel(tr("<small>Пустое поле — колонка не задаётся. Коллекции можно вводить "
                               "в JSON (<code>[\"a\",\"b\"]</code>, <code>{\"k\":1}</code>) или как литерал CQL.</small>"));
    note->setWordWrap(true);
    m_body->addWidget(note);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *inner = new QWidget;
    auto *form = new QFormLayout(inner);

    for (int i = 0; i < m_columns.size(); ++i) {
        const ColumnInfo &c = m_columns.at(i);
        QString label = QStringLiteral("%1 <span style='color:gray'>%2</span>")
                            .arg(c.name.toHtmlEscaped(), c.type.toHtmlEscaped());
        if (c.isPartitionKey()) label = QStringLiteral("<b>%1</b> 🔑").arg(label);
        else if (c.isClustering()) label = QStringLiteral("<b>%1</b> ↕").arg(label);

        auto *edit = new QLineEdit;
        edit->setPlaceholderText(c.isPrimaryKey() ? tr("обязательно") : c.type);
        if (!prefill.isEmpty()) {
            // prefill идёт в порядке исходных колонок
            int src = -1;
            for (int k = 0; k < columns.size(); ++k)
                if (columns.at(k).name == c.name) { src = k; break; }
            if (src >= 0 && src < prefill.size() && !prefill.at(src).isNull())
                edit->setText(Cql::displayValue(prefill.at(src)));
        }
        auto *lbl = new QLabel(label);
        lbl->setTextFormat(Qt::RichText);
        form->addRow(lbl, edit);
        m_edits << edit;
        connect(edit, &QLineEdit::textChanged, this, &InsertRowDialog::updatePreview);
    }
    scroll->setWidget(inner);
    m_body->addWidget(scroll);

    auto *opts = new QHBoxLayout;
    m_ifNotExists = new QCheckBox(tr("IF NOT EXISTS"));
    m_ttl = new QSpinBox;
    m_ttl->setRange(0, 630720000);
    m_ttl->setSpecialValueText(tr("без TTL"));
    m_ttl->setSuffix(tr(" с"));
    opts->addWidget(m_ifNotExists);
    opts->addStretch();
    opts->addWidget(new QLabel(tr("TTL:")));
    opts->addWidget(m_ttl);
    m_body->addLayout(opts);

    connect(m_ifNotExists, &QCheckBox::toggled, this, &InsertRowDialog::updatePreview);
    connect(m_ttl, &QSpinBox::valueChanged, this, &InsertRowDialog::updatePreview);

    finishLayout();
    resize(560, qMin(720, 260 + int(m_columns.size()) * 34));
}

QString InsertRowDialog::validate() const
{
    for (int i = 0; i < m_columns.size(); ++i)
        if (m_columns.at(i).isPrimaryKey() && m_edits.at(i)->text().trimmed().isEmpty())
            return tr("Не задана ключевая колонка «%1»").arg(m_columns.at(i).name);
    return {};
}

QString InsertRowDialog::statement() const
{
    QStringList names, values;
    for (int i = 0; i < m_columns.size(); ++i) {
        const QString text = m_edits.at(i)->text();
        if (text.isEmpty()) continue;
        names << Cql::quoteIdent(m_columns.at(i).name);
        values << Cql::textToLiteral(text, m_columns.at(i).type);
    }
    QString s = QStringLiteral("INSERT INTO %1 (%2)\nVALUES (%3)")
                    .arg(Cql::qualified(m_ks, m_table), names.join(", "), values.join(", "));
    if (m_ifNotExists->isChecked()) s += QStringLiteral(" IF NOT EXISTS");
    if (m_ttl->value() > 0) s += QStringLiteral(" USING TTL %1").arg(m_ttl->value());
    return s + QLatin1Char(';');
}

// ---------------------------------------------------------------------------
TablePropertiesDialog::TablePropertiesDialog(const QString &ks, const QString &table,
                                             const QJsonObject &props, QWidget *parent)
    : CqlFormDialog(tr("Параметры таблицы %1.%2").arg(ks, table), parent)
    , m_ks(ks), m_table(table)
{
    static const QStringList skip = { "keyspace_name", "table_name", "id", "flags", "extensions",
                                      "view_name", "base_table_id", "base_table_name",
                                      "include_all_columns", "where_clause" };
    for (auto it = props.begin(); it != props.end(); ++it)
        if (!skip.contains(it.key()) && !it.value().isArray())
            m_props.insert(it.key(), it.value());

    auto *note = new QLabel(tr("<small>Измените значения в колонке «Новое значение». "
                               "Map-параметры (compaction, compression, caching) — в JSON или литералом CQL.</small>"));
    note->setWordWrap(true);
    m_body->addWidget(note);

    m_grid = new QTableWidget(int(m_props.size()), 3);
    m_grid->setHorizontalHeaderLabels({ tr("Параметр"), tr("Текущее значение"), tr("Новое значение") });
    m_grid->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_grid->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_grid->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_grid->verticalHeader()->hide();

    int row = 0;
    for (auto it = m_props.begin(); it != m_props.end(); ++it, ++row) {
        auto *name = new QTableWidgetItem(it.key());
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        auto *cur = new QTableWidgetItem(Cql::displayValue(it.value()));
        cur->setFlags(cur->flags() & ~Qt::ItemIsEditable);
        cur->setToolTip(Cql::prettyValue(it.value()));
        auto *val = new QTableWidgetItem(Cql::displayValue(it.value()));
        m_grid->setItem(row, 0, name);
        m_grid->setItem(row, 1, cur);
        m_grid->setItem(row, 2, val);
    }
    m_body->addWidget(m_grid);
    connect(m_grid, &QTableWidget::itemChanged, this, &TablePropertiesDialog::updatePreview);

    finishLayout();
    resize(820, 560);
}

QString TablePropertiesDialog::validate() const
{
    if (statement().isEmpty())
        return tr("Нет изменений");
    return {};
}

QString TablePropertiesDialog::statement() const
{
    QStringList parts;
    for (int r = 0; r < m_grid->rowCount(); ++r) {
        const QString key = m_grid->item(r, 0)->text();
        const QString cur = m_grid->item(r, 1)->text();
        const QString val = m_grid->item(r, 2)->text().trimmed();
        if (val == cur) continue;

        const QJsonValue orig = m_props.value(key);
        QString lit;
        if (orig.isObject())
            lit = Cql::textToLiteral(val, QStringLiteral("map<text, text>"));
        else if (orig.isDouble())
            lit = val;
        else
            lit = Cql::quoteString(val);
        parts << QStringLiteral("%1 = %2").arg(key, lit);
    }
    if (parts.isEmpty()) return {};
    return QStringLiteral("ALTER TABLE %1 WITH %2;")
        .arg(Cql::qualified(m_ks, m_table), parts.join(QStringLiteral("\n  AND ")));
}

// ---------------------------------------------------------------------------
CreateTableDialog::CreateTableDialog(const QString &ks, QWidget *parent)
    : CqlFormDialog(tr("Новая таблица в %1").arg(ks), parent)
    , m_ks(ks)
{
    auto *form = new QFormLayout;
    m_name = new QLineEdit;
    m_ifNotExists = new QCheckBox(tr("IF NOT EXISTS"));
    m_ifNotExists->setChecked(true);
    form->addRow(tr("Имя таблицы:"), m_name);
    form->addRow(QString(), m_ifNotExists);
    m_body->addLayout(form);

    m_grid = new QTableWidget(0, 3);
    m_grid->setHorizontalHeaderLabels({ tr("Колонка"), tr("Тип"), tr("Роль") });
    m_grid->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_grid->verticalHeader()->hide();
    m_body->addWidget(m_grid);

    auto *btns = new QHBoxLayout;
    auto *add = new QPushButton(tr("+ колонка"));
    auto *del = new QPushButton(tr("− удалить выбранную"));
    btns->addWidget(add);
    btns->addWidget(del);
    btns->addStretch();
    m_body->addLayout(btns);

    auto *optForm = new QFormLayout;
    m_options = new QLineEdit;
    m_options->setPlaceholderText(QStringLiteral("default_time_to_live = 86400 AND comment = '...'"));
    optForm->addRow(tr("Доп. параметры WITH:"), m_options);
    m_body->addLayout(optForm);

    connect(add, &QPushButton::clicked, this, [this] { addColumnRow(); updatePreview(); });
    connect(del, &QPushButton::clicked, this, [this] {
        const int r = m_grid->currentRow();
        if (r >= 0) { m_grid->removeRow(r); updatePreview(); }
    });
    connect(m_name, &QLineEdit::textChanged, this, &CreateTableDialog::updatePreview);
    connect(m_ifNotExists, &QCheckBox::toggled, this, &CreateTableDialog::updatePreview);
    connect(m_options, &QLineEdit::textChanged, this, &CreateTableDialog::updatePreview);
    connect(m_grid, &QTableWidget::itemChanged, this, &CreateTableDialog::updatePreview);

    addColumnRow(QStringLiteral("id"), QStringLiteral("uuid"), 1);
    addColumnRow(QStringLiteral("name"), QStringLiteral("text"), 0);

    finishLayout();
    resize(640, 560);
}

void CreateTableDialog::addColumnRow(const QString &name, const QString &type, int role)
{
    const int r = m_grid->rowCount();
    m_grid->insertRow(r);
    m_grid->setItem(r, 0, new QTableWidgetItem(name));

    auto *t = typeCombo(type);
    m_grid->setCellWidget(r, 1, t);
    connect(t, &QComboBox::currentTextChanged, this, &CreateTableDialog::updatePreview);

    auto *roleBox = new QComboBox;
    roleBox->addItems({ tr("обычная"), tr("partition key"), tr("clustering ASC"),
                        tr("clustering DESC"), tr("static") });
    roleBox->setCurrentIndex(role);
    m_grid->setCellWidget(r, 2, roleBox);
    connect(roleBox, &QComboBox::currentIndexChanged, this, &CreateTableDialog::updatePreview);
}

QString CreateTableDialog::tableName() const { return m_name->text().trimmed(); }

QString CreateTableDialog::validate() const
{
    if (tableName().isEmpty()) return tr("Укажите имя таблицы");
    bool hasPk = false;
    for (int r = 0; r < m_grid->rowCount(); ++r) {
        if (!m_grid->cellWidget(r, 1) || !m_grid->cellWidget(r, 2)) continue; // строка ещё создаётся
        const auto *item = m_grid->item(r, 0);
        if (!item || item->text().trimmed().isEmpty()) return tr("Пустое имя колонки в строке %1").arg(r + 1);
        if (static_cast<QComboBox *>(m_grid->cellWidget(r, 2))->currentIndex() == 1) hasPk = true;
    }
    if (!hasPk) return tr("Нужна хотя бы одна колонка partition key");
    return {};
}

QString CreateTableDialog::statement() const
{
    QStringList defs, partition, clustering, order;
    bool anyDesc = false;
    for (int r = 0; r < m_grid->rowCount(); ++r) {
        if (!m_grid->cellWidget(r, 1) || !m_grid->cellWidget(r, 2)) continue;
        const auto *item = m_grid->item(r, 0);
        const QString name = Cql::quoteIdent(item ? item->text().trimmed() : QString());
        const QString type = static_cast<QComboBox *>(m_grid->cellWidget(r, 1))->currentText().trimmed();
        const int role = static_cast<QComboBox *>(m_grid->cellWidget(r, 2))->currentIndex();
        defs << QStringLiteral("    %1 %2%3").arg(name, type, role == 4 ? QStringLiteral(" STATIC") : QString());
        if (role == 1) partition << name;
        if (role == 2 || role == 3) {
            clustering << name;
            order << QStringLiteral("%1 %2").arg(name, QString::fromLatin1(role == 3 ? "DESC" : "ASC"));
            anyDesc |= role == 3;
        }
    }
    QString pk = partition.size() == 1 ? partition.first()
                                       : QStringLiteral("(%1)").arg(partition.join(", "));
    if (!clustering.isEmpty()) pk += QStringLiteral(", ") + clustering.join(", ");
    defs << QStringLiteral("    PRIMARY KEY (%1)").arg(pk);

    QStringList with;
    if (anyDesc) with << QStringLiteral("CLUSTERING ORDER BY (%1)").arg(order.join(", "));
    if (!m_options->text().trimmed().isEmpty()) with << m_options->text().trimmed();

    QString s = QStringLiteral("CREATE TABLE %1%2 (\n%3\n)")
                    .arg(m_ifNotExists->isChecked() ? QStringLiteral("IF NOT EXISTS ") : QString(),
                         Cql::qualified(m_ks, tableName()), defs.join(",\n"));
    if (!with.isEmpty()) s += QStringLiteral(" WITH ") + with.join(QStringLiteral(" AND "));
    return s + QLatin1Char(';');
}

// ---------------------------------------------------------------------------
CreateKeyspaceDialog::CreateKeyspaceDialog(QWidget *parent)
    : CqlFormDialog(tr("Новый keyspace"), parent)
{
    auto *form = new QFormLayout;
    m_name = new QLineEdit;
    m_strategy = new QComboBox;
    m_strategy->addItems({ QStringLiteral("NetworkTopologyStrategy"), QStringLiteral("SimpleStrategy") });
    m_rf = new QSpinBox;
    m_rf->setRange(1, 15);
    m_rf->setValue(3);
    m_dcs = new QLineEdit;
    m_dcs->setPlaceholderText(QStringLiteral("dc1: 3, dc2: 2  (пусто — replication_factor для всех DC)"));
    m_durable = new QCheckBox(tr("durable_writes"));
    m_durable->setChecked(true);

    form->addRow(tr("Имя:"), m_name);
    form->addRow(tr("Стратегия:"), m_strategy);
    form->addRow(tr("Replication factor:"), m_rf);
    form->addRow(tr("Дата-центры:"), m_dcs);
    form->addRow(QString(), m_durable);
    m_body->addLayout(form);

    auto upd = [this] {
        m_dcs->setEnabled(m_strategy->currentIndex() == 0);
        updatePreview();
    };
    connect(m_name, &QLineEdit::textChanged, this, upd);
    connect(m_strategy, &QComboBox::currentIndexChanged, this, upd);
    connect(m_rf, &QSpinBox::valueChanged, this, upd);
    connect(m_dcs, &QLineEdit::textChanged, this, upd);
    connect(m_durable, &QCheckBox::toggled, this, upd);
    finishLayout();
    resize(520, height());
}

QString CreateKeyspaceDialog::validate() const
{
    if (!identRe().match(m_name->text().trimmed()).hasMatch())
        return tr("Имя: латинские буквы, цифры и «_», начинается с буквы");
    return {};
}

QString CreateKeyspaceDialog::statement() const
{
    QStringList repl;
    if (m_strategy->currentIndex() == 1) {
        repl << QStringLiteral("'class': 'SimpleStrategy'")
             << QStringLiteral("'replication_factor': %1").arg(m_rf->value());
    } else {
        repl << QStringLiteral("'class': 'NetworkTopologyStrategy'");
        const QStringList dcs = m_dcs->text().split(',', Qt::SkipEmptyParts);
        bool any = false;
        for (const QString &d : dcs) {
            const QStringList kv = d.split(':');
            if (kv.size() != 2) continue;
            repl << QStringLiteral("%1: %2").arg(Cql::quoteString(kv.at(0).trimmed()),
                                                 QString::number(kv.at(1).trimmed().toInt()));
            any = true;
        }
        if (!any) repl << QStringLiteral("'replication_factor': %1").arg(m_rf->value());
    }
    return QStringLiteral("CREATE KEYSPACE IF NOT EXISTS %1\n  WITH replication = {%2}\n  AND durable_writes = %3;")
        .arg(Cql::quoteIdent(m_name->text().trimmed()), repl.join(", "),
             QString::fromLatin1(m_durable->isChecked() ? "true" : "false"));
}

// ---------------------------------------------------------------------------
CellEditDialog::CellEditDialog(const QString &column, const QString &type, const QString &value,
                               QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Значение %1").arg(column));
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(tr("<b>%1</b> <i>%2</i>").arg(column.toHtmlEscaped(), type.toHtmlEscaped())));
    m_edit = new QPlainTextEdit(value);
    m_edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    lay->addWidget(m_edit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);
    resize(620, 420);
}

QString CellEditDialog::text() const { return m_edit->toPlainText(); }
