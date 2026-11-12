#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;

class LoginDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LoginDialog(const QString &server, QWidget *parent = nullptr);

    QString user() const;
    QString password() const;
    void setError(const QString &error);

    void accept() override;

private:
    QString    m_server;
    QLineEdit *m_user;
    QLineEdit *m_password;
    QCheckBox *m_remember;
    QLabel    *m_error;
};
