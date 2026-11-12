#pragma once

#include "CqlUtil.h"

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QVBoxLayout;

// База для диалогов, формирующих оператор CQL с предпросмотром.
class CqlFormDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CqlFormDialog(const QString &title, QWidget *parent = nullptr);
    virtual QString statement() const = 0;

protected:
    // Вызывать в конце конструктора наследника.
    void finishLayout();
    void updatePreview();
    virtual QString validate() const { return {}; }

    QVBoxLayout    *m_body;

private:
    QPlainTextEdit *m_preview = nullptr;
    QLabel         *m_error = nullptr;
    QPushButton    *m_ok = nullptr;
};

// ALTER TABLE ... ADD
class AddColumnDialog : public CqlFormDialog
{
    Q_OBJECT
public:
    AddColumnDialog(const QString &ks, const QString &table, QWidget *parent = nullptr);
    QString statement() const override;

protected:
    QString validate() const override;

private:
    QString    m_ks, m_table;
    QLineEdit *m_name;
    QComboBox *m_type;
    QCheckBox *m_static;
};

// ALTER TABLE ... RENAME a TO b (разрешено только для колонок первичного ключа)
class RenameColumnDialog : public CqlFormDialog
{
    Q_OBJECT
public:
    RenameColumnDialog(const QString &ks, const QString &table, const QString &column,
                       QWidget *parent = nullptr);
    QString statement() const override;

private:
    QString    m_ks, m_table, m_column;
    QLineEdit *m_newName;
};

// INSERT INTO ...
class InsertRowDialog : public CqlFormDialog
{
    Q_OBJECT
public:
    InsertRowDialog(const QString &ks, const QString &table, const QVector<ColumnInfo> &columns,
                    const QJsonArray &prefill = {}, QWidget *parent = nullptr);
    QString statement() const override;

protected:
    QString validate() const override;

private:
    QString             m_ks, m_table;
    QVector<ColumnInfo> m_columns;
    QVector<QLineEdit*> m_edits;
    QSpinBox           *m_ttl;
    QCheckBox          *m_ifNotExists;
};

// ALTER TABLE ... WITH ...
class TablePropertiesDialog : public CqlFormDialog
{
    Q_OBJECT
public:
    TablePropertiesDialog(const QString &ks, const QString &table, const QJsonObject &props,
                          QWidget *parent = nullptr);
    QString statement() const override;

protected:
    QString validate() const override;

private:
    QString       m_ks, m_table;
    QJsonObject   m_props;
    QTableWidget *m_grid;
};

// CREATE TABLE
class CreateTableDialog : public CqlFormDialog
{
    Q_OBJECT
public:
    CreateTableDialog(const QString &ks, QWidget *parent = nullptr);
    QString statement() const override;
    QString tableName() const;

protected:
    QString validate() const override;

private:
    void addColumnRow(const QString &name = {}, const QString &type = QStringLiteral("text"),
                      int role = 0);

    QString       m_ks;
    QLineEdit    *m_name;
    QTableWidget *m_grid;
    QCheckBox    *m_ifNotExists;
    QLineEdit    *m_options;
};

// CREATE KEYSPACE
class CreateKeyspaceDialog : public CqlFormDialog
{
    Q_OBJECT
public:
    explicit CreateKeyspaceDialog(QWidget *parent = nullptr);
    QString statement() const override;

protected:
    QString validate() const override;

private:
    QLineEdit *m_name;
    QComboBox *m_strategy;
    QSpinBox  *m_rf;
    QLineEdit *m_dcs;
    QCheckBox *m_durable;
};

// Редактирование длинного значения ячейки в отдельном окне.
class CellEditDialog : public QDialog
{
    Q_OBJECT
public:
    CellEditDialog(const QString &column, const QString &type, const QString &value,
                   QWidget *parent = nullptr);
    QString text() const;

private:
    QPlainTextEdit *m_edit;
};
