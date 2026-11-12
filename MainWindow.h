#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>

#include <functional>

class CellHoverPopup;
class LogWidget;
class QAction;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QSplitter;
class QTableView;
class QTreeView;
class ResultModel;
class SchemaFilterProxy;
class SchemaModel;
class ValueFilterProxy;
class WsClient;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *ev) override;

private:
    using Done = std::function<void(bool ok, const QJsonObject &reply)>;

    // --- UI
    void setupUi();
    QWidget *createTreePanel();
    QWidget *createEditorPanel();
    QWidget *createDataPanel();
    void setupLogDock();
    void restoreSettings();
    void updateActions();
    void log(const QString &level, const QString &msg);

    // --- Серверы и подключение
    void loadServerHistory();
    void addServerToHistory(const QString &url);
    void removeCurrentServer();
    void connectToServer();
    void disconnectFromServer();
    void onConnected();
    void onDisconnected();
    void showLogin(const QString &error = {});

    // --- Схема
    void loadSchema(std::function<void()> after = {});
    void onTreeClicked(const QModelIndex &proxyIndex);
    void treeContextMenu(const QPoint &pos);

    // --- Запросы
    void execCql(const QString &cql, Done done = {}, bool quiet = false);
    void runEditor();
    void runStatements(const QStringList &statements, int index);
    void openTable(const QString &ks, const QString &table);
    void reloadCurrent();
    void showResult(const QJsonObject &reply, const QString &ks, const QString &table, const QString &cql);
    bool bindSelect(const QString &cql, QString *ks, QString *table) const;

    // --- Редактирование данных
    void onCellEditRequested(int row, int column, const QString &text);
    void refreshRow(int row, const QString &where);
    void editCellInDialog(const QModelIndex &proxyIndex);
    void setCellNull(const QModelIndex &proxyIndex);
    void insertRow(const QJsonArray &prefill = {});
    void deleteSelectedRows();
    void copySelection();
    void gridContextMenu(const QPoint &pos);
    void headerContextMenu(const QPoint &pos);

    // --- Структура таблиц
    void addColumn(const QString &ks, const QString &table);
    void dropColumn(const QString &ks, const QString &table, const QString &column);
    void renameColumn(const QString &ks, const QString &table, const QString &column);
    void tableProperties(const QString &ks, const QString &table);
    void createTable(const QString &ks);
    void createKeyspace();
    void dropObject(const QString &what, const QString &cql);
    void runDdl(const QString &cql, std::function<void()> after = {});

    // --- Поиск и графики
    void applyValueSearch();
    void openChart(QList<int> columns = {});

    QList<int> selectedSourceRows() const;
    QString currentTableKs() const;
    QString currentTableName() const;

    // --- Данные
    WsClient          *m_ws;
    SchemaModel       *m_schema;
    SchemaFilterProxy *m_schemaProxy;
    ResultModel       *m_result;
    ValueFilterProxy  *m_resultProxy;

    // --- Виджеты
    QComboBox      *m_address = nullptr;
    QSpinBox       *m_limit = nullptr;
    QTreeView      *m_tree = nullptr;
    QLineEdit      *m_treeSearch = nullptr;
    QCheckBox      *m_hideSystem = nullptr;
    QPlainTextEdit *m_editor = nullptr;
    QLabel         *m_keyspaceLabel = nullptr;
    QTableView     *m_grid = nullptr;
    CellHoverPopup *m_popup = nullptr;
    QLineEdit      *m_valueSearch = nullptr;
    QComboBox      *m_searchColumn = nullptr;
    QComboBox      *m_searchMode = nullptr;
    QCheckBox      *m_searchCase = nullptr;
    QLabel         *m_tableLabel = nullptr;
    QLabel         *m_rowsLabel = nullptr;
    QLabel         *m_statusConn = nullptr;
    QLabel         *m_statusQuery = nullptr;
    LogWidget      *m_log = nullptr;
    QDockWidget    *m_logDock = nullptr;
    QSplitter      *m_mainSplit = nullptr;
    QSplitter      *m_rightSplit = nullptr;

    // --- Действия
    QAction *m_connectAct = nullptr;
    QAction *m_disconnectAct = nullptr;
    QAction *m_removeServerAct = nullptr;
    QAction *m_refreshSchemaAct = nullptr;
    QAction *m_runAct = nullptr;
    QAction *m_refreshTableAct = nullptr;
    QAction *m_addRowAct = nullptr;
    QAction *m_deleteRowAct = nullptr;
    QAction *m_addColumnAct = nullptr;
    QAction *m_tablePropsAct = nullptr;
    QAction *m_chartAct = nullptr;
    QAction *m_createKsAct = nullptr;

    // --- Состояние
    QString m_user;
    bool    m_authorized = false;
    QString m_currentKeyspace;   // после USE
    QString m_lastQuery;         // последний SELECT, показанный в таблице
    QString m_lastKs, m_lastTable;
    QString m_autoQuery;         // запрос, подставленный при клике по дереву
    int     m_running = 0;
};
