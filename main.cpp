#include "MainWindow.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("ScyllaWs"));
    QApplication::setApplicationName(QStringLiteral("ScyllaWsClient"));
    QApplication::setApplicationVersion(QStringLiteral("1.0"));

    // Стандартные диалоги Qt на русском (если перевод установлен).
    QTranslator qtTr;
    if (qtTr.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                  QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTr);

    MainWindow w;
    w.show();
    return app.exec();
}
