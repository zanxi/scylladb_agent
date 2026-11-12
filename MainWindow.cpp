#include "MainWindow.h"

#include "CellHoverPopup.h"
#include "ChartDialog.h"
#include "CqlHighlighter.h"
#include "CqlUtil.h"
#include "LogWidget.h"
#include "LoginDialog.h"
#include "ResultModel.h"
#include "SchemaModel.h"
#include "TableDialogs.h"
#include "WsClient.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>
#include <memory>

namespace
{
constexpr int kMaxHistory = 25;
constexpr int kDefaultPort = 8765;

QString normalizeUrl(QString s)
{
    s = s.trimmed();
    if (s.isEmpty()) return {};
    if (!s.contains(QLatin1String("://")))
        s.prepend(QStringLiteral("ws://"));
    QUrl url(s, QUrl::TolerantMode);
    if (!url.isValid() || url.host().isEmpty()) return {};
    if (url.scheme() != QLatin1String("ws") && url.scheme() != QLatin1String("wss")) return {};
    if (url.port() == -1) url.setPort(kDefaultPort);
    return url.toString();
}

QString unquoteIdent(const QString &s)
{
    if (s.size() >= 2 && s.startsWith('"') && s.endsWith('"'))
        return s.mid(1, s.size() - 2).replace(QStringLiteral("\"\""), QStringLiteral("\""));
    return s.toLower();
}

bool isDdl(const QString &cql)
{
    static const QRegularExpression re(QStringLiteral("^\\s*(CREATE|ALTER|DROP)\\b"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re.match(cql).hasMatch();
}

QString shortCql(const QString &cql)
{
    QString s = cql.simplified();
    return s.size() > 400 ? s.left(400) + QChar(0x2026) : s;
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_ws(new WsClient(this))
    , m_schema(new SchemaModel(this))
    , m_schemaProxy(new SchemaFilterProxy(this))
    , m_result(new ResultModel(this))
    , m_resultProxy(new ValueFilterProxy(this))
{
    setWindowTitle(tr("ScyllaDB WebSocket Client"));
    m_schemaProxy->setSourceModel(m_schema);
    m_resultProxy->setSourceModel(m_result);

    setupUi();
    setupLogDock();
    loadServerHistory();
    restoreSettings();

    connect(m_ws, &WsClient::log, this, &MainWindow::log);
    connect(m_ws, &WsClient::connected, this, &MainWindow::onConnected);
    connect(m_ws, &WsClient::disconnected, this, &MainWindow::onDisconnected);
    connect(m_ws, &WsClient::errorOccurred, this, [this](const QString &msg) {
        m_statusConn->setText(tr("Ошибка: %1").arg(msg));
        updateActions();
    });
    m_ws->setSslErrorHandler([this](const QList<QSslError> &errors) {
        QStringList msgs;
//        for (const auto &e : errors) msgs << e.errorString();
        return QMessageBox::warning(this, tr("Сертификат сервера"),
                                    tr("Проблемы с сертификатом:\n%1\n\nВсё равно подключиться?")
                                        .arg(msgs.join('\n')),
                                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
    });

    connect(m_result, &ResultModel::editRequested, this, &MainWindow::onCellEditRequested);

    updateActions();
    log(QStringLiteral("INFO"), tr("Готово. Введите адрес сервера (ws://host:port) и нажмите «Подключиться»."));
}

MainWindow::~MainWindow() = default;

// ===========================================================================
// UI
// ===========================================================================
void MainWindow::setupUi()
{
    QStyle *st = style();

    // --- Действия
    m_connectAct = new QAction(st->standardIcon(QStyle::SP_DialogApplyButton), tr("Подключиться"), this);
    m_disconnectAct = new QAction(st->standardIcon(QStyle::SP_DialogCloseButton), tr("Отключиться"), this);
    m_removeServerAct = new QAction(st->standardIcon(QStyle::SP_TrashIcon), tr("Удалить адрес из списка"), this);
    m_refreshSchemaAct = new QAction(st->standardIcon(QStyle::SP_BrowserReload), tr("Обновить структуру"), this);
    m_refreshSchemaAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    m_runAct = new QAction(st->standardIcon(QStyle::SP_MediaPlay), tr("Выполнить"), this);
    m_runAct->setShortcuts({ QKeySequence(QStringLiteral("Ctrl+Return")), QKeySequence(QStringLiteral("Ctrl+Enter")) });
    m_runAct->setToolTip(tr("Выполнить выделенный текст или весь скрипт (Ctrl+Enter)"));
    m_refreshTableAct = new QAction(st->standardIcon(QStyle::SP_BrowserReload), tr("Обновить данные"), this);
    m_refreshTableAct->setShortcut(QKeySequence::Refresh);
    m_addRowAct = new QAction(tr("+ Строка"), this);
    m_addRowAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Ins")));
    m_deleteRowAct = new QAction(tr("− Строка"), this);
    m_addColumnAct = new QAction(tr("+ Колонка"), this);
    m_tablePropsAct = new QAction(tr("Параметры таблицы"), this);
    m_chartAct = new QAction(tr("График"), this);
    m_chartAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    m_createKsAct = new QAction(tr("Создать keyspace…"), this);

    connect(m_connectAct, &QAction::triggered, this, &MainWindow::connectToServer);
    connect(m_disconnectAct, &QAction::triggered, this, &MainWindow::disconnectFromServer);
    connect(m_removeServerAct, &QAction::triggered, this, &MainWindow::removeCurrentServer);
    connect(m_refreshSchemaAct, &QAction::triggered, this, [this] { loadSchema(); });
    connect(m_runAct, &QAction::triggered, this, &MainWindow::runEditor);
    connect(m_refreshTableAct, &QAction::triggered, this, &MainWindow::reloadCurrent);
    connect(m_addRowAct, &QAction::triggered, this, [this] { insertRow(); });
    connect(m_deleteRowAct, &QAction::triggered, this, &MainWindow::deleteSelectedRows);
    connect(m_addColumnAct, &QAction::triggered, this, [this] { addColumn(currentTableKs(), currentTableName()); });
    connect(m_tablePropsAct, &QAction::triggered, this, [this] { tableProperties(currentTableKs(), currentTableName()); });
    connect(m_chartAct, &QAction::triggered, this, [this] { openChart(); });
    connect(m_createKsAct, &QAction::triggered, this, &MainWindow::createKeyspace);

    // --- Адресная строка
    auto *tb = addToolBar(tr("Сервер"));
    tb->setObjectName(QStringLiteral("serverToolbar"));
    tb->setMovable(false);
    tb->addWidget(new QLabel(tr(" Сервер: ")));
    m_address = new QComboBox;
    m_address->setEditable(true);
    m_address->setInsertPolicy(QComboBox::NoInsert);
    m_address->setMinimumWidth(360);
    m_address->setMaxVisibleItems(15);
    m_address->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_address->lineEdit()->setPlaceholderText(QStringLiteral("ws://127.0.0.1:8765"));
    m_address->lineEdit()->setClearButtonEnabled(true);
    m_address->setToolTip(tr("Адрес WebSocket-сервера. Список пополняется после успешного входа."));
    connect(m_address->lineEdit(), &QLineEdit::returnPressed, this, &MainWindow::connectToServer);
    tb->addWidget(m_address);
    tb->addAction(m_connectAct);
    tb->addAction(m_disconnectAct);
    tb->addAction(m_removeServerAct);
    tb->addSeparator();
    tb->addAction(m_refreshSchemaAct);
    tb->addSeparator();
    tb->addWidget(new QLabel(tr(" Лимит строк: ")));
    m_limit = new QSpinBox;
    m_limit->setRange(1, 1000000);
    m_limit->setValue(500);
    m_limit->setSingleStep(100);
    m_limit->setToolTip(tr("LIMIT для просмотра таблицы и максимум строк в ответе (ограничен --max-rows сервера)"));
    tb->addWidget(m_limit);

    // --- Центральная часть
    m_mainSplit = new QSplitter(Qt::Horizontal);
    m_mainSplit->addWidget(createTreePanel());

    m_rightSplit = new QSplitter(Qt::Vertical);
    m_rightSplit->addWidget(createEditorPanel());
    m_rightSplit->addWidget(createDataPanel());
    m_rightSplit->setStretchFactor(1, 1);
    m_rightSplit->setSizes({ 180, 520 });

    m_mainSplit->addWidget(m_rightSplit);
    m_mainSplit->setStretchFactor(1, 1);
    m_mainSplit->setSizes({ 300, 900 });
    setCentralWidget(m_mainSplit);

    // --- Меню
    QMenu *fileMenu = menuBar()->addMenu(tr("&Сервер"));
    fileMenu->addAction(m_connectAct);
    fileMenu->addAction(m_disconnectAct);
    fileMenu->addAction(m_removeServerAct);
    fileMenu->addSeparator();
    QAction *quitAct = fileMenu->addAction(tr("Выход"), this, &QWidget::close);
    quitAct->setShortcut(QKeySequence::Quit);

    QMenu *dbMenu = menuBar()->addMenu(tr("&База"));
    dbMenu->addAction(m_refreshSchemaAct);
    dbMenu->addAction(m_createKsAct);
    dbMenu->addAction(tr("Создать таблицу…"), this, [this] {
        QString ks = currentTableKs();
        if (ks.isEmpty()) ks = m_currentKeyspace;
        if (ks.isEmpty()) {
            const QModelIndex idx = m_schemaProxy->mapToSource(m_tree->currentIndex());
            ks = idx.data(SchemaModel::KeyspaceRole).toString();
        }
        if (ks.isEmpty()) {
            QMessageBox::information(this, tr("Создать таблицу"), tr("Выберите keyspace в дереве."));
            return;
        }
        createTable(ks);
    });

    QMenu *dataMenu = menuBar()->addMenu(tr("&Данные"));
    dataMenu->addAction(m_runAct);
    dataMenu->addAction(m_refreshTableAct);
    dataMenu->addSeparator();
    dataMenu->addAction(m_addRowAct);
    dataMenu->addAction(m_deleteRowAct);
    dataMenu->addAction(m_addColumnAct);
    dataMenu->addAction(m_tablePropsAct);
    dataMenu->addSeparator();
    dataMenu->addAction(m_chartAct);

    // --- Строка состояния
    m_statusConn = new QLabel(tr("Не подключено"));
    m_statusQuery = new QLabel;
    statusBar()->addWidget(m_statusConn, 1);
    statusBar()->addPermanentWidget(m_statusQuery);
}

QWidget *MainWindow::createTreePanel()
{
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(4, 4, 0, 4);

    m_treeSearch = new QLineEdit;
    m_treeSearch->setPlaceholderText(tr("Поиск по названию keyspace / таблицы / колонки…"));
    m_treeSearch->setClearButtonEnabled(true);
    lay->addWidget(m_treeSearch);

    m_hideSystem = new QCheckBox(tr("Скрыть системные keyspace"));
    lay->addWidget(m_hideSystem);

    m_tree = new QTreeView;
    m_tree->setModel(m_schemaProxy);
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(0, Qt::AscendingOrder);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(m_tree);

    connect(m_treeSearch, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_schemaProxy->setFilterFixedString(t);
        if (t.isEmpty()) m_tree->collapseAll();
        else m_tree->expandAll();
    });
    connect(m_hideSystem, &QCheckBox::toggled, m_schemaProxy, &SchemaFilterProxy::setHideSystem);
    connect(m_tree, &QTreeView::clicked, this, &MainWindow::onTreeClicked);
    connect(m_tree, &QTreeView::customContextMenuRequested, this, &MainWindow::treeContextMenu);

    auto *find = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")), this);
    connect(find, &QShortcut::activated, this, [this] { m_treeSearch->setFocus(); m_treeSearch->selectAll(); });
    return w;
}

QWidget *MainWindow::createEditorPanel()
{
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(4, 4, 4, 0);

    auto *bar = new QHBoxLayout;
    auto *runBtn = new QToolButton;
    runBtn->setDefaultAction(m_runAct);
    runBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bar->addWidget(runBtn);
    auto *clearBtn = new QPushButton(tr("Очистить"));
    bar->addWidget(clearBtn);
    bar->addStretch();
    m_keyspaceLabel = new QLabel;
    bar->addWidget(m_keyspaceLabel);
    lay->addLayout(bar);

    m_editor = new QPlainTextEdit;
    m_editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_editor->setPlaceholderText(tr("CQL-запрос, например: SELECT * FROM ks.table WHERE id = 1;\n"
                                    "Несколько операторов разделяются «;». Ctrl+Enter — выполнить."));
    m_editor->setTabStopDistance(QFontMetricsF(m_editor->font()).horizontalAdvance(' ') * 4);
    new CqlHighlighter(m_editor->document());
    m_editor->addAction(m_runAct);
    lay->addWidget(m_editor);

    connect(clearBtn, &QPushButton::clicked, m_editor, &QPlainTextEdit::clear);
    return w;
}

QWidget *MainWindow::createDataPanel()
{
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);
    lay->setContentsMargins(4, 4, 4, 4);

    // Строка 1: заголовок и действия над таблицей
    auto *top = new QHBoxLayout;
    m_tableLabel = new QLabel(tr("<i>Выберите таблицу слева или выполните запрос</i>"));
    top->addWidget(m_tableLabel, 1);
    auto *actBar = new QToolBar;
    actBar->setIconSize(QSize(16, 16));
    actBar->addAction(m_refreshTableAct);
    actBar->addSeparator();
    actBar->addAction(m_addRowAct);
    actBar->addAction(m_deleteRowAct);
    actBar->addAction(m_addColumnAct);
    actBar->addAction(m_tablePropsAct);
    actBar->addSeparator();
    actBar->addAction(m_chartAct);
    top->addWidget(actBar);
    lay->addLayout(top);

    // Строка 2: поиск по значению
    auto *search = new QHBoxLayout;
    search->addWidget(new QLabel(tr("Поиск:")));
    m_valueSearch = new QLineEdit;
    m_valueSearch->setPlaceholderText(tr("значение…"));
    m_valueSearch->setClearButtonEnabled(true);
    search->addWidget(m_valueSearch, 1);
    search->addWidget(new QLabel(tr("в")));
    m_searchColumn = new QComboBox;
    m_searchColumn->setMinimumWidth(140);
    m_searchColumn->addItem(tr("всех колонках"), -1);
    search->addWidget(m_searchColumn);
    m_searchMode = new QComboBox;
    m_searchMode->addItems({ tr("содержит"), tr("равно"), tr("regex") });
    search->addWidget(m_searchMode);
    m_searchCase = new QCheckBox(tr("Аа"));
    m_searchCase->setToolTip(tr("Учитывать регистр"));
    search->addWidget(m_searchCase);
    m_rowsLabel = new QLabel;
    m_rowsLabel->setMinimumWidth(130);
    m_rowsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    search->addWidget(m_rowsLabel);
    lay->addLayout(search);

    connect(m_valueSearch, &QLineEdit::textChanged, this, &MainWindow::applyValueSearch);
    connect(m_searchColumn, &QComboBox::currentIndexChanged, this, &MainWindow::applyValueSearch);
    connect(m_searchMode, &QComboBox::currentIndexChanged, this, &MainWindow::applyValueSearch);
    connect(m_searchCase, &QCheckBox::toggled, this, &MainWindow::applyValueSearch);

    // Таблица
    m_grid = new QTableView;
    m_grid->setModel(m_resultProxy);
    m_grid->setSortingEnabled(true);
    m_grid->sortByColumn(-1, Qt::AscendingOrder);
    m_grid->setAlternatingRowColors(true);
    m_grid->setWordWrap(false);
    m_grid->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_grid->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_grid->setContextMenuPolicy(Qt::CustomContextMenu);
    m_grid->horizontalHeader()->setSectionsMovable(true);
    m_grid->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    m_grid->horizontalHeader()->setResizeContentsPrecision(50);
    m_grid->verticalHeader()->setDefaultSectionSize(m_grid->fontMetrics().height() + 8);
    lay->addWidget(m_grid);

    m_popup = new CellHoverPopup(m_grid);

    connect(m_grid, &QTableView::customContextMenuRequested, this, &MainWindow::gridContextMenu);
    connect(m_grid->horizontalHeader(), &QHeaderView::customContextMenuRequested,
            this, &MainWindow::headerContextMenu);
    connect(m_grid->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateActions);

    auto *copy = new QAction(this);
    copy->setShortcut(QKeySequence::Copy);
    copy->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(copy, &QAction::triggered, this, &MainWindow::copySelection);
    m_grid->addAction(copy);

    auto *del = new QAction(this);
    del->setShortcut(QKeySequence::Delete);
    del->setShortcutContext(Qt::WidgetShortcut);
    connect(del, &QAction::triggered, this, &MainWindow::deleteSelectedRows);
    m_grid->addAction(del);

    auto *findAct = new QShortcut(QKeySequence::Find, this);
    connect(findAct, &QShortcut::activated, this, [this] { m_valueSearch->setFocus(); m_valueSearch->selectAll(); });
    return w;
}

void MainWindow::setupLogDock()
{
    m_log = new LogWidget;
    m_logDock = new QDockWidget(tr("Лог операций"), this);
    m_logDock->setObjectName(QStringLiteral("logDock"));
    m_logDock->setWidget(m_log);
    addDockWidget(Qt::BottomDockWidgetArea, m_logDock);
    QAction *toggle = m_logDock->toggleViewAction();
    toggle->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));
    menuBar()->addMenu(tr("&Вид"))->addAction(toggle);
}

void MainWindow::restoreSettings()
{
    QSettings s;
    restoreGeometry(s.value(QStringLiteral("ui/geometry")).toByteArray());
    restoreState(s.value(QStringLiteral("ui/state")).toByteArray());
    m_mainSplit->restoreState(s.value(QStringLiteral("ui/mainSplit")).toByteArray());
    m_rightSplit->restoreState(s.value(QStringLiteral("ui/rightSplit")).toByteArray());
    m_limit->setValue(s.value(QStringLiteral("ui/limit"), 500).toInt());
    m_hideSystem->setChecked(s.value(QStringLiteral("ui/hideSystem"), false).toBool());
    if (geometry().width() < 400) resize(1300, 850);
}

void MainWindow::closeEvent(QCloseEvent *ev)
{
    QSettings s;
    s.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    s.setValue(QStringLiteral("ui/state"), saveState());
    s.setValue(QStringLiteral("ui/mainSplit"), m_mainSplit->saveState());
    s.setValue(QStringLiteral("ui/rightSplit"), m_rightSplit->saveState());
    s.setValue(QStringLiteral("ui/limit"), m_limit->value());
    s.setValue(QStringLiteral("ui/hideSystem"), m_hideSystem->isChecked());
    s.setValue(QStringLiteral("ui/lastServer"), m_address->currentText());
    m_ws->close();
    ev->accept();
}

void MainWindow::updateActions()
{
    const bool online = m_ws->isConnected() && m_authorized;
    const bool bound = m_result->isBound();
    const bool editable = m_result->isEditable();
    const bool hasSel = m_grid && m_grid->selectionModel() && m_grid->selectionModel()->hasSelection();

    m_connectAct->setEnabled(!m_ws->isConnected());
    m_disconnectAct->setEnabled(m_ws->isConnected());
    m_address->setEnabled(!m_ws->isConnected());
    m_removeServerAct->setEnabled(!m_ws->isConnected() && m_address->currentIndex() >= 0);
    m_refreshSchemaAct->setEnabled(online);
    m_createKsAct->setEnabled(online);
    m_runAct->setEnabled(online);
    m_refreshTableAct->setEnabled(online && !m_lastQuery.isEmpty());
    m_addRowAct->setEnabled(online && bound && !m_schema->isView(m_lastKs, m_lastTable));
    m_deleteRowAct->setEnabled(online && editable && hasSel);
    m_addColumnAct->setEnabled(online && bound && !m_schema->isView(m_lastKs, m_lastTable));
    m_tablePropsAct->setEnabled(online && bound);
    m_chartAct->setEnabled(m_result->rowCount() > 0);

    m_keyspaceLabel->setText(m_currentKeyspace.isEmpty()
                                 ? QString()
                                 : tr("USE <b>%1</b>").arg(m_currentKeyspace.toHtmlEscaped()));
}

void MainWindow::log(const QString &level, const QString &msg)
{
    m_log->append(level, msg);
}

// ===========================================================================
// Серверы и подключение
// ===========================================================================
void MainWindow::loadServerHistory()
{
    QSettings s;
    const QStringList list = s.value(QStringLiteral("servers")).toStringList();
    m_address->clear();
    m_address->addItems(list);
    const QString last = s.value(QStringLiteral("ui/lastServer")).toString();
    m_address->setCurrentText(last.isEmpty() && !list.isEmpty() ? list.first() : last);
}

void MainWindow::addServerToHistory(const QString &url)
{
    QSettings s;
    QStringList list = s.value(QStringLiteral("servers")).toStringList();
    list.removeAll(url);
    list.prepend(url);
    while (list.size() > kMaxHistory) list.removeLast();
    s.setValue(QStringLiteral("servers"), list);

    const QSignalBlocker block(m_address);
    m_address->clear();
    m_address->addItems(list);
    m_address->setCurrentIndex(0);
}

void MainWindow::removeCurrentServer()
{
    const QString url = m_address->currentText();
    QSettings s;
    QStringList list = s.value(QStringLiteral("servers")).toStringList();
    if (!list.removeAll(url)) return;
    s.setValue(QStringLiteral("servers"), list);
    m_address->removeItem(m_address->findText(url));
    log(QStringLiteral("INFO"), tr("Адрес %1 удалён из списка").arg(url));
    updateActions();
}

void MainWindow::connectToServer()
{
    if (m_ws->isConnected()) return;
    const QString url = normalizeUrl(m_address->currentText());
    if (url.isEmpty()) {
        QMessageBox::warning(this, tr("Адрес сервера"),
                             tr("Некорректный адрес. Пример: ws://127.0.0.1:8765 или wss://host:8765"));
        return;
    }
    m_address->setCurrentText(url);
    m_user.clear();
    m_authorized = false;
    m_statusConn->setText(tr("Подключение к %1…").arg(url));
    m_ws->open(QUrl(url));
    updateActions();
}

void MainWindow::disconnectFromServer()
{
    if (m_ws->isConnected())
        m_ws->request(QStringLiteral("logout"), {});
    m_ws->close();
}

void MainWindow::onConnected()
{
    m_statusConn->setText(tr("Соединение установлено, требуется вход"));
    updateActions();
    QTimer::singleShot(0, this, [this] { showLogin(); });
}

void MainWindow::onDisconnected()
{
    m_user.clear();
    m_authorized = false;
    m_currentKeyspace.clear();
    m_running = 0;
    m_statusConn->setText(tr("Не подключено"));
    m_statusQuery->clear();
    m_schema->load(QJsonObject());
    updateActions();
}

void MainWindow::showLogin(const QString &error)
{
    if (!m_ws->isConnected()) return;
    LoginDialog dlg(m_ws->url().toString(), this);
    dlg.setError(error);
    if (dlg.exec() != QDialog::Accepted) {
        log(QStringLiteral("WARN"), tr("Вход отменён"));
        m_ws->close();
        return;
    }
    const QString user = dlg.user();
    m_statusConn->setText(tr("Авторизация…"));
    log(QStringLiteral("INFO"), tr("Авторизация пользователя «%1»").arg(user.isEmpty() ? tr("(без имени)") : user));

    m_ws->request(QStringLiteral("auth"), QJsonObject{ { "user", user }, { "password", dlg.password() } },
        [this, user](const QJsonObject &r, qint64 ms) {
            if (!r.value("ok").toBool()) {
                const QString err = r.value("error").toString();
                log(QStringLiteral("ERROR"), tr("Ошибка авторизации: %1").arg(err));
                QTimer::singleShot(0, this, [this, err] { showLogin(err); });
                return;
            }
            m_user = user;
            m_authorized = true;
            const QString version = r.value("server_version").toString();
            log(QStringLiteral("OK"), tr("Вход выполнен (%1 мс). ScyllaDB %2").arg(ms).arg(version));
            m_statusConn->setText(tr("%1 — %2 — ScyllaDB %3")
                                      .arg(m_ws->url().toString(),
                                           user.isEmpty() ? tr("анонимно") : user, version));
            addServerToHistory(m_ws->url().toString());
            updateActions();
            loadSchema();
        });
}

// ===========================================================================
// Схема
// ===========================================================================
void MainWindow::loadSchema(std::function<void()> after)
{
    // Запоминаем раскрытые узлы и текущий элемент.
    QSet<QString> expanded;
    for (int i = 0; i < m_schemaProxy->rowCount(); ++i) {
        const QModelIndex ks = m_schemaProxy->index(i, 0);
        if (!m_tree->isExpanded(ks)) continue;
        expanded << ks.data(SchemaModel::KeyspaceRole).toString();
        for (int j = 0; j < m_schemaProxy->rowCount(ks); ++j) {
            const QModelIndex t = m_schemaProxy->index(j, 0, ks);
            if (m_tree->isExpanded(t))
                expanded << t.data(SchemaModel::KeyspaceRole).toString() + '.' + t.data(SchemaModel::TableRole).toString();
        }
    }

    log(QStringLiteral("INFO"), tr("Загрузка структуры базы…"));
    m_ws->request(QStringLiteral("schema"), {}, [this, expanded, after](const QJsonObject &r, qint64 ms) {
        if (!r.value("ok").toBool()) {
            log(QStringLiteral("ERROR"), tr("Не удалось получить структуру: %1").arg(r.value("error").toString()));
            return;
        }
        m_schema->load(r);
        int tables = 0;
        for (const auto &k : r.value("keyspaces").toArray())
            tables += k.toObject().value("tables").toArray().size();
        log(QStringLiteral("OK"), tr("Структура загружена: keyspace %1, таблиц %2 (%3 мс)")
                                      .arg(r.value("keyspaces").toArray().size()).arg(tables).arg(ms));

        for (int i = 0; i < m_schemaProxy->rowCount(); ++i) {
            const QModelIndex ks = m_schemaProxy->index(i, 0);
            if (expanded.contains(ks.data(SchemaModel::KeyspaceRole).toString()))
                m_tree->expand(ks);
            for (int j = 0; j < m_schemaProxy->rowCount(ks); ++j) {
                const QModelIndex t = m_schemaProxy->index(j, 0, ks);
                if (expanded.contains(t.data(SchemaModel::KeyspaceRole).toString() + '.' +
                                      t.data(SchemaModel::TableRole).toString()))
                    m_tree->expand(t);
            }
        }
        if (!m_treeSearch->text().isEmpty())
            m_tree->expandAll();
        updateActions();
        if (after) after();
    });
}

void MainWindow::onTreeClicked(const QModelIndex &proxyIndex)
{
    const QModelIndex idx = m_schemaProxy->mapToSource(proxyIndex);
    const int type = idx.data(SchemaModel::NodeTypeRole).toInt();
    const QString ks = idx.data(SchemaModel::KeyspaceRole).toString();
    const QString table = idx.data(SchemaModel::TableRole).toString();

    if (type == SchemaModel::TableNode || type == SchemaModel::ViewNode) {
        openTable(ks, table);
    } else if (type == SchemaModel::ColumnNode) {
        const QString col = idx.data(SchemaModel::ColumnRole).toString();
        auto focusColumn = [this, col] {
            const int c = m_result->columnIndex(col);
            if (c >= 0 && m_resultProxy->rowCount() > 0)
                m_grid->scrollTo(m_resultProxy->index(0, c));
            if (c >= 0) m_grid->selectColumn(c);
        };
        if (m_lastKs == ks && m_lastTable == table)
            focusColumn();
        else
            openTable(ks, table);
    }
}

void MainWindow::treeContextMenu(const QPoint &pos)
{
    const QModelIndex pidx = m_tree->indexAt(pos);
    const QModelIndex idx = m_schemaProxy->mapToSource(pidx);
    const int type = idx.data(SchemaModel::NodeTypeRole).toInt();
    const QString ks = idx.data(SchemaModel::KeyspaceRole).toString();
    const QString table = idx.data(SchemaModel::TableRole).toString();
    const QString column = idx.data(SchemaModel::ColumnRole).toString();
    const bool online = m_ws->isConnected() && m_authorized;

    QMenu menu(this);
    menu.addAction(m_refreshSchemaAct);
    menu.addAction(m_createKsAct);

    if (pidx.isValid()) {
        menu.addSeparator();
        menu.addAction(tr("Копировать имя"), this, [idx] {
            QApplication::clipboard()->setText(idx.data(SchemaModel::NameRole).toString());
        });
    }

    if (type == SchemaModel::KeyspaceNode) {
        menu.addSeparator();
        menu.addAction(tr("USE %1").arg(ks), this, [this, ks] { execCql(QStringLiteral("USE %1;").arg(Cql::quoteIdent(ks))); });
        menu.addAction(tr("Создать таблицу…"), this, [this, ks] { createTable(ks); })->setEnabled(online);
        menu.addAction(tr("Показать DDL (DESCRIBE)"), this, [this, ks] {
            const QString q = QStringLiteral("DESCRIBE KEYSPACE %1;").arg(Cql::quoteIdent(ks));
            m_editor->setPlainText(q);
            runEditor();
        });
        menu.addSeparator();
        menu.addAction(style()->standardIcon(QStyle::SP_TrashIcon), tr("Удалить keyspace…"), this, [this, ks] {
            dropObject(tr("keyspace «%1» со всеми таблицами и данными").arg(ks),
                       QStringLiteral("DROP KEYSPACE %1;").arg(Cql::quoteIdent(ks)));
        })->setEnabled(online && !SchemaModel::isSystemKeyspace(ks));
    } else if (type == SchemaModel::TableNode || type == SchemaModel::ViewNode) {
        const bool view = type == SchemaModel::ViewNode;
        menu.addSeparator();
        menu.addAction(tr("Открыть данные"), this, [this, ks, table] { openTable(ks, table); });
        menu.addAction(tr("SELECT в редактор"), this, [this, ks, table] {
            m_editor->setPlainText(QStringLiteral("SELECT * FROM %1 LIMIT %2;")
                                       .arg(Cql::qualified(ks, table)).arg(m_limit->value()));
            m_editor->setFocus();
        });
        menu.addAction(tr("SELECT COUNT(*)"), this, [this, ks, table] {
            m_editor->setPlainText(QStringLiteral("SELECT COUNT(*) FROM %1;").arg(Cql::qualified(ks, table)));
            runEditor();
        });
        menu.addAction(tr("Показать DDL (DESCRIBE)"), this, [this, ks, table, view] {
            m_editor->setPlainText(QStringLiteral("DESCRIBE %1 %2;")
                                       .arg(QString::fromLatin1(view ? "MATERIALIZED VIEW" : "TABLE"), Cql::qualified(ks, table)));
            runEditor();
        });
        menu.addSeparator();
        if (!view) {
            menu.addAction(tr("Добавить строку…"), this, [this, ks, table] {
                if (m_lastKs != ks || m_lastTable != table) openTable(ks, table);
                InsertRowDialog dlg(ks, table, m_schema->columns(ks, table), {}, this);
                if (dlg.exec() == QDialog::Accepted)
                    execCql(dlg.statement(), [this](bool ok, const QJsonObject &) { if (ok) reloadCurrent(); });
            })->setEnabled(online);
            menu.addAction(tr("Добавить колонку…"), this, [this, ks, table] { addColumn(ks, table); })->setEnabled(online);
        }
        menu.addAction(tr("Параметры таблицы…"), this, [this, ks, table] { tableProperties(ks, table); })->setEnabled(online);
        menu.addSeparator();
        if (!view) {
            menu.addAction(tr("Очистить (TRUNCATE)…"), this, [this, ks, table] {
                dropObject(tr("все данные таблицы «%1.%2»").arg(ks, table),
                           QStringLiteral("TRUNCATE %1;").arg(Cql::qualified(ks, table)));
            })->setEnabled(online);
        }
        menu.addAction(style()->standardIcon(QStyle::SP_TrashIcon),
                       view ? tr("Удалить view…") : tr("Удалить таблицу…"), this, [this, ks, table, view] {
            dropObject(tr("%1 «%2.%3»").arg(view ? tr("materialized view") : tr("таблицу"), ks, table),
                       QStringLiteral("DROP %1 %2;").arg(QString::fromLatin1(view ? "MATERIALIZED VIEW" : "TABLE"), Cql::qualified(ks, table)));
        })->setEnabled(online && !SchemaModel::isSystemKeyspace(ks));
    } else if (type == SchemaModel::ColumnNode) {
        const QString kind = idx.data(SchemaModel::KindRole).toString();
        const bool pk = kind == QLatin1String("partition_key") || kind == QLatin1String("clustering");
        menu.addSeparator();
        menu.addAction(tr("Переименовать…"), this, [this, ks, table, column] { renameColumn(ks, table, column); })
            ->setEnabled(online && pk);
        menu.addAction(style()->standardIcon(QStyle::SP_TrashIcon), tr("Удалить колонку…"), this,
                       [this, ks, table, column] { dropColumn(ks, table, column); })
            ->setEnabled(online && !pk && !m_schema->isView(ks, table));
    }

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

// ===========================================================================
// Запросы
// ===========================================================================
void MainWindow::execCql(const QString &cql, Done done, bool quiet)
{
    if (!quiet)
        log(QStringLiteral("CQL"), shortCql(cql));
    ++m_running;
    m_statusQuery->setText(tr("Выполняется…"));
    QApplication::setOverrideCursor(Qt::BusyCursor);

    m_ws->query(cql, m_limit->value(), [this, cql, done, quiet](const QJsonObject &r, qint64 ms) {
        QApplication::restoreOverrideCursor();
        m_running = qMax(0, m_running - 1);
        const bool ok = r.value("ok").toBool();
        if (ok) {
            const int rows = r.value("rows").toArray().size();
            const bool hasCols = !r.value("columns").toArray().isEmpty();
            const QString msg = hasCols ? tr("OK: %1 строк%2, %3 мс (сервер %4 мс)")
                                              .arg(rows)
                                              .arg(r.value("truncated").toBool() ? tr(" (обрезано)") : QString())
                                              .arg(ms).arg(r.value("elapsed_ms").toDouble())
                                        : tr("OK, %1 мс").arg(ms);
            if (!quiet) log(QStringLiteral("OK"), msg);
            m_statusQuery->setText(msg);

            static const QRegularExpression useRe(QStringLiteral("^\\s*USE\\s+(\"[^\"]+\"|\\w+)"),
                                                  QRegularExpression::CaseInsensitiveOption);
            const auto m = useRe.match(cql);
            if (m.hasMatch()) {
                m_currentKeyspace = unquoteIdent(m.captured(1));
                updateActions();
            }
        } else {
            const QString err = r.value("error").toString();
            log(QStringLiteral("ERROR"), quiet ? tr("%1\n%2").arg(err, shortCql(cql)) : err);
            m_statusQuery->setText(tr("Ошибка: %1").arg(err.left(120)));
        }
        if (done) done(ok, r);
    });
}

void MainWindow::runEditor()
{
    QString text = m_editor->textCursor().selectedText();
    text.replace(QChar::ParagraphSeparator, '\n');
    if (text.trimmed().isEmpty())
        text = m_editor->toPlainText();
    const QStringList statements = Cql::splitStatements(text);
    if (statements.isEmpty()) return;
    m_autoQuery.clear();
    runStatements(statements, 0);
}

void MainWindow::runStatements(const QStringList &statements, int index)
{
    if (index >= statements.size()) return;
    const QString cql = statements.at(index);

    execCql(cql, [this, statements, index, cql](bool ok, const QJsonObject &r) {
        if (!ok) {
            if (statements.size() > 1)
                log(QStringLiteral("WARN"), tr("Скрипт остановлен на операторе %1 из %2").arg(index + 1).arg(statements.size()));
            return;
        }
        if (!r.value("columns").toArray().isEmpty()) {
            QString ks, table;
            bindSelect(cql, &ks, &table);
            showResult(r, ks, table, cql);
        }
        if (isDdl(cql)) {
            loadSchema([this, statements, index] { runStatements(statements, index + 1); });
            return;
        }
        runStatements(statements, index + 1);
    });
}

bool MainWindow::bindSelect(const QString &cql, QString *ks, QString *table) const
{
    static const QRegularExpression re(
        QStringLiteral("^\\s*SELECT\\s+(?:JSON\\s+)?(?:DISTINCT\\s+)?.+?\\s+FROM\\s+(\"(?:[^\"]|\"\")+\"|\\w+)(?:\\s*\\.\\s*(\"(?:[^\"]|\"\")+\"|\\w+))?"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(cql);
    if (!m.hasMatch()) return false;
    if (cql.contains(QRegularExpression(QStringLiteral("^\\s*SELECT\\s+JSON\\b"),
                                        QRegularExpression::CaseInsensitiveOption)))
        return false; // SELECT JSON — одна колонка [json], не редактируем
    if (m.captured(2).isEmpty()) {
        *ks = m_currentKeyspace;
        *table = unquoteIdent(m.captured(1));
    } else {
        *ks = unquoteIdent(m.captured(1));
        *table = unquoteIdent(m.captured(2));
    }
    return m_schema->hasTable(*ks, *table);
}

void MainWindow::openTable(const QString &ks, const QString &table)
{
    const QString cql = QStringLiteral("SELECT * FROM %1 LIMIT %2;")
                            .arg(Cql::qualified(ks, table)).arg(m_limit->value());
    // Подставляем запрос в редактор, только если там пусто или прошлый автозапрос.
    const QString cur = m_editor->toPlainText().trimmed();
    if (cur.isEmpty() || cur == m_autoQuery) {
        m_editor->setPlainText(cql);
        m_autoQuery = cql;
    }
    execCql(cql, [this, ks, table, cql](bool ok, const QJsonObject &r) {
        if (ok) showResult(r, ks, table, cql);
    });
}

void MainWindow::reloadCurrent()
{
    if (m_lastQuery.isEmpty()) return;
    const QString ks = m_lastKs, table = m_lastTable, cql = m_lastQuery;
    execCql(cql, [this, ks, table, cql](bool ok, const QJsonObject &r) {
        if (ok) showResult(r, ks, table, cql);
    });
}

void MainWindow::showResult(const QJsonObject &reply, const QString &ks, const QString &table, const QString &cql)
{
    QVector<ColumnInfo> cols;
    for (const auto &cv : reply.value("columns").toArray()) {
        const QJsonObject co = cv.toObject();
        ColumnInfo c;
        c.name = co.value("name").toString();
        c.type = co.value("type").toString();
        cols << c;
    }

    bool bound = !table.isEmpty() && m_schema->hasTable(ks, table);
    bool editable = false;
    if (bound) {
        const QVector<ColumnInfo> tcols = m_schema->columns(ks, table);
        QSet<QString> present;
        for (ColumnInfo &c : cols) {
            for (const ColumnInfo &t : tcols) {
                if (t.name == c.name) {
                    c.kind = t.kind;
                    c.position = t.position;
                    c.clusteringOrder = t.clusteringOrder;
                    present << c.name;
                    break;
                }
            }
        }
        editable = !m_schema->isView(ks, table);
        for (const ColumnInfo &t : tcols)
            if (t.isPrimaryKey() && !present.contains(t.name)) editable = false;
    }

    m_result->setResult(cols, reply.value("rows").toArray(), bound ? ks : QString(),
                        bound ? table : QString(), editable);
    m_lastQuery = cql;
    m_lastKs = bound ? ks : QString();
    m_lastTable = bound ? table : QString();

    // Список колонок для поиска
    {
        const QSignalBlocker b(m_searchColumn);
        const QString prev = m_searchColumn->currentText();
        m_searchColumn->clear();
        m_searchColumn->addItem(tr("всех колонках"), -1);
        for (int i = 0; i < cols.size(); ++i)
            m_searchColumn->addItem(cols.at(i).name, i);
        const int pi = m_searchColumn->findText(prev);
        m_searchColumn->setCurrentIndex(pi >= 0 ? pi : 0);
    }
    applyValueSearch();

    // Ширина колонок
    m_grid->resizeColumnsToContents();
    for (int c = 0; c < cols.size(); ++c)
        if (m_grid->columnWidth(c) > 360) m_grid->setColumnWidth(c, 360);

    QString title = bound ? QStringLiteral("<b>%1.%2</b>").arg(ks.toHtmlEscaped(), table.toHtmlEscaped())
                          : tr("<b>Результат запроса</b>");
    if (bound && !editable)
        title += tr(" <span style='color:gray'>(только чтение%1)</span>")
                     .arg(m_schema->isView(ks, table) ? tr(": view") : tr(": в выборке нет всех колонок ключа"));
    else if (editable)
        title += tr(" <span style='color:gray'>— двойной щелчок по ячейке для правки</span>");
    if (reply.value("truncated").toBool())
        title += tr(" <span style='color:#ef6c00'>показаны первые %1 строк</span>").arg(m_result->rowCount());
    m_tableLabel->setText(title);
    updateActions();
}

// ===========================================================================
// Редактирование данных
// ===========================================================================
void MainWindow::onCellEditRequested(int row, int column, const QString &text)
{
    if (!m_result->isEditable()) return;
    const ColumnInfo &col = m_result->column(column);
    const QString where = m_result->whereClause(row);
    if (where.isEmpty()) {
        log(QStringLiteral("ERROR"), tr("Не удалось определить первичный ключ строки"));
        return;
    }
    const QString cql = QStringLiteral("UPDATE %1 SET %2 = %3 WHERE %4;")
                            .arg(Cql::qualified(m_result->keyspace(), m_result->table()),
                                 Cql::quoteIdent(col.name), Cql::textToLiteral(text, col.type), where);
    execCql(cql, [this, row, where](bool ok, const QJsonObject &) {
        if (ok) refreshRow(row, where);
    });
}

void MainWindow::refreshRow(int row, const QString &where)
{
    QStringList names;
    for (const ColumnInfo &c : m_result->columns())
        names << (c.kind.isEmpty() ? c.name : Cql::quoteIdent(c.name));
    const QString ks = m_result->keyspace(), table = m_result->table();
    const QString cql = QStringLiteral("SELECT %1 FROM %2 WHERE %3;")
                            .arg(names.join(", "), Cql::qualified(ks, table), where);
    execCql(cql, [this, row, ks, table](bool ok, const QJsonObject &r) {
        if (!ok || m_result->keyspace() != ks || m_result->table() != table) return;
        const QJsonArray rows = r.value("rows").toArray();
        if (rows.size() == 1)
            m_result->setRowValues(row, rows.first().toArray());
        else if (rows.isEmpty())
            m_result->removeRowAt(row); // строка исчезла (например, все значения стали null)
    }, true);
}

void MainWindow::editCellInDialog(const QModelIndex &proxyIndex)
{
    const QModelIndex src = m_resultProxy->mapToSource(proxyIndex);
    if (!(m_result->flags(src) & Qt::ItemIsEditable)) return;
    const ColumnInfo &col = m_result->column(src.column());
    const QJsonValue v = m_result->value(src.row(), src.column());
    CellEditDialog dlg(col.name, col.type, v.isNull() ? QString() : Cql::prettyValue(v), this);
    if (dlg.exec() == QDialog::Accepted)
        onCellEditRequested(src.row(), src.column(), dlg.text());
}

void MainWindow::setCellNull(const QModelIndex &proxyIndex)
{
    const QModelIndex src = m_resultProxy->mapToSource(proxyIndex);
    if (!(m_result->flags(src) & Qt::ItemIsEditable)) return;
    const ColumnInfo &col = m_result->column(src.column());
    const QString where = m_result->whereClause(src.row());
    if (where.isEmpty()) return;
    const int row = src.row();
    execCql(QStringLiteral("UPDATE %1 SET %2 = null WHERE %3;")
                .arg(Cql::qualified(m_result->keyspace(), m_result->table()), Cql::quoteIdent(col.name), where),
            [this, row, where](bool ok, const QJsonObject &) { if (ok) refreshRow(row, where); });
}

void MainWindow::insertRow(const QJsonArray &prefill)
{
    const QString ks = currentTableKs(), table = currentTableName();
    if (table.isEmpty()) return;
    const QVector<ColumnInfo> tcols = m_schema->columns(ks, table);

    // prefill приходит в порядке колонок результата — переложим в порядок схемы.
    QJsonArray ordered;
    if (!prefill.isEmpty()) {
        for (const ColumnInfo &c : tcols) {
            const int i = m_result->columnIndex(c.name);
            ordered.append(i >= 0 ? prefill.at(i) : QJsonValue(QJsonValue::Null));
        }
    }
    InsertRowDialog dlg(ks, table, tcols, ordered, this);
    if (dlg.exec() != QDialog::Accepted) return;
    execCql(dlg.statement(), [this](bool ok, const QJsonObject &r) {
        if (!ok) return;
        // IF NOT EXISTS возвращает [applied]
        const QJsonArray rows = r.value("rows").toArray();
        const QJsonArray first = rows.isEmpty() ? QJsonArray() : rows.first().toArray();
        if (!first.isEmpty() && first.first().isBool() && !first.first().toBool())
            log(QStringLiteral("WARN"), tr("Строка с таким ключом уже существует — вставка не выполнена"));
        reloadCurrent();
    });
}

QList<int> MainWindow::selectedSourceRows() const
{
    QSet<int> rows;
    for (const QModelIndex &i : m_grid->selectionModel()->selectedIndexes())
        rows << m_resultProxy->mapToSource(i).row();
    QList<int> out(rows.begin(), rows.end());
    std::sort(out.begin(), out.end(), std::greater<int>());
    return out;
}

void MainWindow::deleteSelectedRows()
{
    if (!m_result->isEditable()) return;
    const QList<int> rows = selectedSourceRows();
    if (rows.isEmpty()) return;
    if (QMessageBox::question(this, tr("Удаление"),
                              tr("Удалить строк: %1 из %2.%3?").arg(rows.size())
                                  .arg(m_result->keyspace(), m_result->table())) != QMessageBox::Yes)
        return;

    const QString target = Cql::qualified(m_result->keyspace(), m_result->table());
    // Строки удаляются по убыванию индекса — индексы оставшихся не сдвигаются.
    // Цепочку держат живой только ожидающие ответа колбэки (weak_ptr внутри — без цикла).
    auto step = std::make_shared<std::function<void(int)>>();
    std::weak_ptr<std::function<void(int)>> weak = step;
    *step = [this, rows, target, weak](int i) {
        auto self = weak.lock();
        if (!self || i >= rows.size()) return;
        const int row = rows.at(i);
        const QString where = m_result->whereClause(row);
        if (where.isEmpty()) { (*self)(i + 1); return; }
        execCql(QStringLiteral("DELETE FROM %1 WHERE %2;").arg(target, where),
                [this, row, i, self](bool ok, const QJsonObject &) {
                    if (!ok) return;
                    m_result->removeRowAt(row);
                    (*self)(i + 1);
                });
    };
    (*step)(0);
}

void MainWindow::copySelection()
{
    const QModelIndexList sel = m_grid->selectionModel()->selectedIndexes();
    if (sel.isEmpty()) return;
    if (sel.size() == 1) {
        QApplication::clipboard()->setText(sel.first().data(ResultModel::RawTextRole).toString());
        return;
    }
    int r0 = INT_MAX, r1 = -1, c0 = INT_MAX, c1 = -1;
    for (const auto &i : sel) {
        r0 = qMin(r0, i.row()); r1 = qMax(r1, i.row());
        c0 = qMin(c0, i.column()); c1 = qMax(c1, i.column());
    }
    QSet<QPair<int, int>> chosen;
    for (const auto &i : sel) chosen.insert({ i.row(), i.column() });
    QStringList lines;
    QStringList header;
    for (int c = c0; c <= c1; ++c)
        header << m_result->column(m_resultProxy->mapToSource(m_resultProxy->index(r0, c)).column()).name;
    lines << header.join('\t');
    for (int r = r0; r <= r1; ++r) {
        QStringList cells;
        for (int c = c0; c <= c1; ++c)
            cells << (chosen.contains({ r, c }) ? m_resultProxy->index(r, c).data(ResultModel::RawTextRole).toString()
                                                : QString());
        lines << cells.join('\t');
    }
    QApplication::clipboard()->setText(lines.join('\n'));
}

void MainWindow::gridContextMenu(const QPoint &pos)
{
    const QModelIndex pidx = m_grid->indexAt(pos);
    const QModelIndex src = m_resultProxy->mapToSource(pidx);
    const bool editable = pidx.isValid() && (m_result->flags(src) & Qt::ItemIsEditable);

    QMenu menu(this);
    if (pidx.isValid()) {
        menu.addAction(tr("Копировать\tCtrl+C"), this, &MainWindow::copySelection);
        menu.addAction(tr("Копировать строку как INSERT"), this, [this, src] {
            QApplication::clipboard()->setText(m_result->insertStatement(src.row()));
        })->setEnabled(m_result->isBound());
        menu.addSeparator();
        menu.addAction(tr("Редактировать в окне…"), this, [this, pidx] { editCellInDialog(pidx); })->setEnabled(editable);
        menu.addAction(tr("Установить NULL"), this, [this, pidx] { setCellNull(pidx); })->setEnabled(editable);
        menu.addSeparator();
        menu.addAction(tr("Дублировать строку…"), this, [this, src] { insertRow(m_result->rowValues(src.row())); })
            ->setEnabled(m_addRowAct->isEnabled());
    }
    menu.addAction(m_addRowAct);
    menu.addAction(m_deleteRowAct);
    menu.addSeparator();
    menu.addAction(m_chartAct);
    menu.addAction(m_refreshTableAct);
    menu.exec(m_grid->viewport()->mapToGlobal(pos));
}

void MainWindow::headerContextMenu(const QPoint &pos)
{
    QHeaderView *h = m_grid->horizontalHeader();
    const int logical = h->logicalIndexAt(pos);
    QMenu menu(this);

    if (logical >= 0 && logical < m_result->columnCount()) {
        const ColumnInfo c = m_result->column(logical);
        const QString ks = m_result->keyspace(), table = m_result->table();
        const bool online = m_ws->isConnected() && m_authorized;
        const bool bound = m_result->isBound() && !c.kind.isEmpty() && !m_schema->isView(ks, table);

        menu.addAction(tr("График по «%1»").arg(c.name), this, [this, logical] { openChart({ logical }); });
        menu.addAction(tr("Искать в этой колонке"), this, [this, logical] {
            m_searchColumn->setCurrentIndex(m_searchColumn->findData(logical));
            m_valueSearch->setFocus();
        });
        menu.addAction(tr("Копировать имя"), this, [c] { QApplication::clipboard()->setText(c.name); });
        menu.addSeparator();
        menu.addAction(tr("Переименовать…"), this, [this, ks, table, c] { renameColumn(ks, table, c.name); })
            ->setEnabled(online && bound && c.isPrimaryKey());
        menu.addAction(style()->standardIcon(QStyle::SP_TrashIcon), tr("Удалить колонку…"), this,
                       [this, ks, table, c] { dropColumn(ks, table, c.name); })
            ->setEnabled(online && bound && !c.isPrimaryKey());
        menu.addSeparator();
        menu.addAction(tr("Скрыть колонку"), this, [h, logical] { h->hideSection(logical); });
    }
    menu.addAction(tr("Показать все колонки"), this, [h] {
        for (int i = 0; i < h->count(); ++i) h->showSection(i);
    });
    menu.addAction(m_addColumnAct);
    menu.exec(h->mapToGlobal(pos));
}

// ===========================================================================
// Структура таблиц
// ===========================================================================
void MainWindow::runDdl(const QString &cql, std::function<void()> after)
{
    execCql(cql, [this, after](bool ok, const QJsonObject &) {
        if (!ok) return;
        loadSchema([after] { if (after) after(); });
    });
}

void MainWindow::addColumn(const QString &ks, const QString &table)
{
    if (table.isEmpty()) return;
    AddColumnDialog dlg(ks, table, this);
    if (dlg.exec() != QDialog::Accepted) return;
    runDdl(dlg.statement(), [this, ks, table] {
        if (m_lastKs == ks && m_lastTable == table) reloadCurrent();
    });
}

void MainWindow::dropColumn(const QString &ks, const QString &table, const QString &column)
{
    if (QMessageBox::warning(this, tr("Удаление колонки"),
                             tr("Удалить колонку «%1» из %2.%3?\nДанные колонки будут потеряны.")
                                 .arg(column, ks, table),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    runDdl(QStringLiteral("ALTER TABLE %1 DROP %2;").arg(Cql::qualified(ks, table), Cql::quoteIdent(column)),
           [this, ks, table] { if (m_lastKs == ks && m_lastTable == table) reloadCurrent(); });
}

void MainWindow::renameColumn(const QString &ks, const QString &table, const QString &column)
{
    RenameColumnDialog dlg(ks, table, column, this);
    if (dlg.exec() != QDialog::Accepted) return;
    runDdl(dlg.statement(), [this, ks, table] {
        if (m_lastKs == ks && m_lastTable == table) openTable(ks, table);
    });
}

void MainWindow::tableProperties(const QString &ks, const QString &table)
{
    if (table.isEmpty()) return;
    const bool view = m_schema->isView(ks, table);
    const QString cql = QStringLiteral("SELECT * FROM system_schema.%1 WHERE keyspace_name = %2 AND %3 = %4;")
                            .arg(QString::fromLatin1(view ? "views" : "tables"), Cql::quoteString(ks),
                                 QString::fromLatin1(view ? "view_name" : "table_name"), Cql::quoteString(table));
    execCql(cql, [this, ks, table, view](bool ok, const QJsonObject &r) {
        if (!ok) return;
        const QJsonArray rows = r.value("rows").toArray();
        if (rows.isEmpty()) { log(QStringLiteral("ERROR"), tr("Таблица не найдена")); return; }
        const QJsonArray cols = r.value("columns").toArray();
        const QJsonArray vals = rows.first().toArray();
        QJsonObject props;
        for (int i = 0; i < cols.size(); ++i)
            props.insert(cols.at(i).toObject().value("name").toString(), vals.at(i));

        TablePropertiesDialog dlg(ks, table, props, this);
        if (dlg.exec() != QDialog::Accepted) return;
        QString stmt = dlg.statement();
        if (view) stmt.replace(QRegularExpression(QStringLiteral("^ALTER TABLE")), QStringLiteral("ALTER MATERIALIZED VIEW"));
        execCql(stmt);
    }, true);
}

void MainWindow::createTable(const QString &ks)
{
    CreateTableDialog dlg(ks, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString name = dlg.tableName();
    runDdl(dlg.statement(), [this, ks, name] { openTable(ks, name.toLower()); });
}

void MainWindow::createKeyspace()
{
    CreateKeyspaceDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted)
        runDdl(dlg.statement());
}

void MainWindow::dropObject(const QString &what, const QString &cql)
{
    if (QMessageBox::warning(this, tr("Подтверждение"),
                             tr("Будет удалено: %1.\n\n%2\n\nОперация необратима. Продолжить?").arg(what, cql),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    runDdl(cql, [this, cql] {
        if (cql.startsWith(QLatin1String("TRUNCATE"))) { reloadCurrent(); return; }
        if (!m_lastTable.isEmpty() && !m_schema->hasTable(m_lastKs, m_lastTable)) {
            m_result->clear();
            m_lastQuery.clear();
            m_lastKs.clear();
            m_lastTable.clear();
            m_tableLabel->setText(tr("<i>Таблица удалена</i>"));
            updateActions();
        }
    });
}

// ===========================================================================
// Поиск и графики
// ===========================================================================
void MainWindow::applyValueSearch()
{
    const int col = m_searchColumn->currentData().toInt();
    m_resultProxy->setSearch(m_valueSearch->text(), col,
                             ValueFilterProxy::Mode(m_searchMode->currentIndex()),
                             m_searchCase->isChecked());
    const int shown = m_resultProxy->rowCount(), total = m_result->rowCount();
    m_rowsLabel->setText(shown == total ? tr("строк: %1").arg(total)
                                        : tr("найдено %1 из %2").arg(shown).arg(total));
}

void MainWindow::openChart(QList<int> columns)
{
    if (m_result->rowCount() == 0) return;
    if (columns.isEmpty()) {
        QSet<int> sel;
        for (const QModelIndex &i : m_grid->selectionModel()->selectedIndexes())
            sel << m_resultProxy->mapToSource(i).column();
        columns = QList<int>(sel.begin(), sel.end());
    }
    if (columns.isEmpty()) { // по умолчанию — первые числовые колонки, кроме ключей
        for (int i = 0; i < m_result->columnCount() && columns.size() < 3; ++i) {
            const ColumnInfo &c = m_result->column(i);
            if (Cql::parseType(c.type).isNumeric() && !c.isPrimaryKey()) columns << i;
        }
    }
    const QString title = m_result->isBound() ? QStringLiteral("%1.%2").arg(m_result->keyspace(), m_result->table())
                                              : tr("результат запроса");
    QVector<QJsonArray> rows;
    rows.reserve(m_resultProxy->rowCount());
    for (int pr = 0; pr < m_resultProxy->rowCount(); ++pr)
        rows << m_result->rowValues(m_resultProxy->mapToSource(m_resultProxy->index(pr, 0)).row());
    auto *dlg = new ChartDialog(m_result->columns(), rows, columns, title, this);
    dlg->show();
}

QString MainWindow::currentTableKs() const { return m_result->keyspace(); }
QString MainWindow::currentTableName() const { return m_result->table(); }
