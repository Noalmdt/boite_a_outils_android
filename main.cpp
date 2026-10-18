#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // Obligatoire pour que QSettings (réglages webhook) et AppDataLocation
    // (file d'attente webhook_queue.jsonl) fonctionnent correctement.
    // À définir AVANT de créer la fenêtre.
    QCoreApplication::setOrganizationName("Noalmdt");
    QCoreApplication::setApplicationName("BoiteAOutilsAndroid");

    MainWindow w;
    w.show();
    return QApplication::exec();
}
