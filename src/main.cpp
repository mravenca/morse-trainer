#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("morse-trainer"));
    QApplication::setApplicationName(QStringLiteral("morse-trainer"));
    QApplication::setApplicationDisplayName(QStringLiteral("Morse Trainer"));

    MainWindow w;
    w.show();
    return app.exec();
}
