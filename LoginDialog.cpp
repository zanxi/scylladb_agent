#include "LoginDialog.h"

#include <QAction>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStyle>
#include <QVBoxLayout>

LoginDialog::LoginDialog(const QString &server, QWidget *parent)
    : QDialog(parent)
    , m_server(server)
{
    setWindowTitle(tr("Авторизация"));
    setMinimumWidth(380);

    auto *lay = new QVBoxLayout(this);

    auto *header = new QLabel(tr("Вход на сервер <b>%1</b>").arg(server.toHtmlEscaped()));
    header->setWordWrap(true);
    lay->addWidget(header);

    auto *form = new QFormLayout;
    m_user = new QLineEdit;
    m_user->setPlaceholderText(tr("cassandra"));
    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);

    auto *eye = m_password->addAction(style()->standardIcon(QStyle::SP_FileDialogInfoView),
                                      QLineEdit::TrailingPosition);
    eye->setCheckable(true);
    eye->setToolTip(tr("Показать пароль"));
    connect(eye, &QAction::toggled, this, [this](bool on) {
        m_password->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
    });

    m_remember = new QCheckBox(tr("Запомнить имя пользователя"));
    form->addRow(tr("Пользователь:"), m_user);
    form->addRow(tr("Пароль:"), m_password);
    form->addRow(QString(), m_remember);
    lay->addLayout(form);

    auto *hint = new QLabel(tr("<small>Учётные данные передаются в ScyllaDB (PasswordAuthenticator). "
                               "Если аутентификация в кластере отключена, поля можно оставить пустыми.</small>"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    m_error = new QLabel;
    m_error->setStyleSheet(QStringLiteral("color:#c62828"));
    m_error->setWordWrap(true);
    m_error->hide();
    lay->addWidget(m_error);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Войти"));
    connect(buttons, &QDialogButtonBox::accepted, this, &LoginDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &LoginDialog::reject);
    lay->addWidget(buttons);

    QSettings s;
    const QString saved = s.value(QStringLiteral("login/") + QString::fromLatin1(server.toUtf8().toHex())).toString();
    m_user->setText(saved);
    m_remember->setChecked(!saved.isEmpty());
    (saved.isEmpty() ? m_user : m_password)->setFocus();
}

QString LoginDialog::user() const { return m_user->text().trimmed(); }
QString LoginDialog::password() const { return m_password->text(); }

void LoginDialog::setError(const QString &error)
{
    m_error->setText(error);
    m_error->setVisible(!error.isEmpty());
}

void LoginDialog::accept()
{
    QSettings s;
    const QString key = QStringLiteral("login/") + QString::fromLatin1(m_server.toUtf8().toHex());
    if (m_remember->isChecked())
        s.setValue(key, user());
    else
        s.remove(key);
    QDialog::accept();
}
