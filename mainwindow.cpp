#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QProcess>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QSimpleUpdater.h>
#include <functional>

// ---------------------------------------------------------------------------
// Configuration : une seule constante de version à changer à chaque release
// ---------------------------------------------------------------------------
#define APP_VERSION "1.0.0"

static const QString URL_UPDATES =
    "https://raw.githubusercontent.com/Noalmdt/boite_a_outils_android/main/updates.json";

// ---------------------------------------------------------------------------
// Utilitaires
// ---------------------------------------------------------------------------
static QString obtenirCheminExecutable(const QString &nomOutil)
{
    // 1. Dans le PATH du système
    QString cheminTrouve = QStandardPaths::findExecutable(nomOutil);
    if (!cheminTrouve.isEmpty()) {
        return cheminTrouve;
    }

    // 2. Dans le dossier platform-tools à côté de l'application
    QString cheminLocal = QCoreApplication::applicationDirPath() + "/platform-tools/" + nomOutil;
#if defined(Q_OS_WIN)
    cheminLocal += ".exe";
#endif
    if (QFile::exists(cheminLocal)) {
        return QDir::toNativeSeparators(cheminLocal);
    }

    // 3. Chemin de secours (Windows)
    QString cheminSecours = "C:/Android/platform-tools/" + nomOutil;
#if defined(Q_OS_WIN)
    cheminSecours += ".exe";
#endif
    return QDir::toNativeSeparators(cheminSecours);
}

static QString dossierParDefaut()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (base.isEmpty()) {
        base = QDir::homePath();
    }
    return base;
}

// Lance un outil (adb / fastboot) sans bloquer l'interface.
// - affiche la sortie en direct dans les logs
// - désactive le bouton pendant l'opération
// - barre de progression en mode "occupé" pendant l'exécution
// - appelle "apresSucces" si la commande se termine correctement
static void lancerProcessus(QObject *parent,
                            QPlainTextEdit *logs,
                            QProgressBar *barre,
                            QWidget *bouton,
                            const QString &outil,
                            const QStringList &arguments,
                            const QString &messageSucces,
                            const QString &messageEchec,
                            std::function<void()> apresSucces = nullptr)
{
    const QString chemin = obtenirCheminExecutable(outil);
    if (!QFile::exists(chemin)) {
        logs->appendPlainText("❌ Outil introuvable : " + outil +
                              "\nPlacez le dossier platform-tools à côté de l'application "
                              "ou ajoutez-le au PATH.");
        return;
    }

    barre->setRange(0, 0);          // mode "occupé"
    if (bouton) bouton->setEnabled(false);

    QProcess *process = new QProcess(parent);
    process->setProgram(chemin);
    process->setArguments(arguments);

    QObject::connect(process, &QProcess::readyReadStandardOutput, parent, [=]() {
        logs->appendPlainText(QString::fromLocal8Bit(process->readAllStandardOutput()).trimmed());
    });
    QObject::connect(process, &QProcess::readyReadStandardError, parent, [=]() {
        // fastboot écrit sa progression normale sur stderr
        logs->appendPlainText(QString::fromLocal8Bit(process->readAllStandardError()).trimmed());
    });

    QObject::connect(process, &QProcess::errorOccurred, parent, [=](QProcess::ProcessError erreur) {
        if (erreur == QProcess::FailedToStart) {
            barre->setRange(0, 100);
            barre->setValue(0);
            if (bouton) bouton->setEnabled(true);
            logs->appendPlainText("❌ Impossible de démarrer : " + chemin);
            process->deleteLater();
        }
    });

    QObject::connect(process, &QProcess::finished, parent,
                     [=](int exitCode, QProcess::ExitStatus exitStatus) {
                         barre->setRange(0, 100);
                         barre->setValue(100);
                         if (bouton) bouton->setEnabled(true);

                         if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                             logs->appendPlainText("\n" + messageSucces);
                             if (apresSucces) apresSucces();
                         } else {
                             logs->appendPlainText("\n❌ " + messageEchec + " (code : " + QString::number(exitCode) + ")");
                         }
                         process->deleteLater();
                     });

    process->start();
}

// ---------------------------------------------------------------------------
// Fenêtre principale
// ---------------------------------------------------------------------------
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->barreProgression->setRange(0, 100);
    ui->barreProgression->setValue(0);
    setWindowTitle(QString("Boîte à outils Android v%1").arg(APP_VERSION));

    // Mise à jour automatique (l'identifiant de QSimpleUpdater est l'URL)
    QSimpleUpdater *updater = QSimpleUpdater::getInstance();
    updater->setModuleVersion(URL_UPDATES, APP_VERSION);
    updater->setNotifyOnUpdate(URL_UPDATES, true);     // popup si une MAJ existe
    updater->setNotifyOnFinish(URL_UPDATES, false);    // silence si déjà à jour
    updater->setDownloaderEnabled(URL_UPDATES, true);  // téléchargement dans l'appli
    updater->checkForUpdates(URL_UPDATES);             // vérification au lancement
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_btnDetecter_clicked()
{
    ui->txtLogs->setPlainText("Détection de l'appareil en cours...\n");
    lancerProcessus(this, ui->txtLogs, ui->barreProgression, ui->btnDetecter,
                    "fastboot", {"devices"},
                    "Détection terminée.",
                    "Échec de la détection");
}

void MainWindow::on_btnUnlock_clicked()
{
    QMessageBox::StandardButton reponse = QMessageBox::question(
        this,
        "ATTENTION - ZONE RISQUÉE",
        "Le déverrouillage va EFFACER toutes vos données. Voulez-vous continuer ?",
        QMessageBox::Yes | QMessageBox::No
        );
    if (reponse == QMessageBox::No) {
        ui->txtLogs->setPlainText("Opération annulée par sécurité.");
        return;
    }

    QString codeSecret = ui->inputCodeSecret->text().trimmed();
    QStringList arguments = {"oem", "unlock"};
    if (!codeSecret.isEmpty()) {
        arguments.append(codeSecret);
    }

    ui->txtLogs->setPlainText("Envoi de la commande de déverrouillage...\n");
    lancerProcessus(this, ui->txtLogs, ui->barreProgression, ui->btnUnlock,
                    "fastboot", arguments,
                    "Commande de déverrouillage envoyée.",
                    "Échec du déverrouillage");
}

void MainWindow::on_btnFlashRecovery_clicked()
{
    QMessageBox::StandardButton reponse = QMessageBox::question(
        this,
        "ATTENTION - ZONE RISQUÉE",
        "Vous allez modifier la partition Recovery. Voulez-vous continuer ?",
        QMessageBox::Yes | QMessageBox::No
        );
    if (reponse == QMessageBox::No) {
        ui->txtLogs->setPlainText("Flashage du Recovery annulé.");
        return;
    }

    QString fichierRecovery = ui->inputPathRecovery->text().trimmed();
    if (fichierRecovery.isEmpty() || !QFile::exists(fichierRecovery)) {
        ui->txtLogs->setPlainText("Erreur : veuillez sélectionner un fichier .img valide avant de flasher !");
        return;
    }

    ui->txtLogs->setPlainText("Début du flashage Recovery...\nFichier : " + fichierRecovery + "\n");
    lancerProcessus(this, ui->txtLogs, ui->barreProgression, ui->btnFlashRecovery,
                    "fastboot", {"flash", "recovery", fichierRecovery},
                    "✅ Flash du Recovery terminé avec succès !",
                    "Échec du flashage. Vérifiez la connexion du téléphone");
}

void MainWindow::on_btnFlashSystem_clicked()
{
    QMessageBox::StandardButton reponse = QMessageBox::question(
        this,
        "ATTENTION - DANGER MAXIMUM",
        "Vous allez écraser le système d'exploitation actuel du téléphone. Êtes-vous ABSOLUMENT sûr ?",
        QMessageBox::Yes | QMessageBox::No
        );
    if (reponse == QMessageBox::No) {
        ui->txtLogs->setPlainText("Flashage du Système annulé.");
        return;
    }

    QString fichierSystem = ui->inputPathSystem->text().trimmed();
    if (fichierSystem.isEmpty() || !QFile::exists(fichierSystem)) {
        ui->txtLogs->setPlainText("Erreur : veuillez sélectionner un fichier .img valide avant de flasher !");
        return;
    }

    ui->txtLogs->setPlainText("⚠️ FLASHAGE SYSTÈME EN COURS...\n"
                              "Cette opération est longue. Ne débranchez sous aucun prétexte le téléphone.\n");
    lancerProcessus(this, ui->txtLogs, ui->barreProgression, ui->btnFlashSystem,
                    "fastboot", {"flash", "system", fichierSystem},
                    "✅ Félicitations ! Le système a été flashé avec succès.",
                    "ERREUR CRITIQUE : le flashage du système a échoué. "
                    "Vérifiez le câble, le port USB, et assurez-vous que le bootloader est déverrouillé");
}

void MainWindow::on_btnDiagnostic_clicked()
{
    ui->txtLogs->setPlainText("Exécution du diagnostic...\n\n--- APPAREILS EN LIGNE ---\n");

    // Première commande : liste des appareils, puis état de la batterie
    lancerProcessus(this, ui->txtLogs, ui->barreProgression, ui->btnDiagnostic,
                    "adb", {"devices"},
                    "",
                    "Échec de la commande adb devices",
                    [this]() {
                        ui->txtLogs->appendPlainText("\n--- ÉTAT DE LA BATTERIE ---");
                        lancerProcessus(this, ui->txtLogs, ui->barreProgression, ui->btnDiagnostic,
                                        "adb", {"shell", "dumpsys", "battery"},
                                        "Diagnostic terminé.",
                                        "Échec de la lecture de la batterie");
                    });
}

void MainWindow::on_btnSauvegarde_clicked()
{
    // Dossier choisi par l'utilisateur, sinon Documents/BoiteAOutilsAndroid
    QString base = ui->inputPathCaseBackup->text().trimmed();
    if (base.isEmpty()) {
        base = dossierParDefaut() + "/BoiteAOutilsAndroid";
    }
    const QString destination = base + "/Sauvegarde_Photos";
    QDir().mkpath(destination);

    ui->txtLogs->setPlainText("Initialisation de la sauvegarde des photos...\n"
                              "Destination : " + QDir::toNativeSeparators(destination) +
                              "\nVeuillez patienter.\n");
    lancerProcessus(this, ui->txtLogs, ui->barreProgression, ui->btnSauvegarde,
                    "adb", {"pull", "/sdcard/DCIM/Camera/", destination},
                    "✅ Sauvegarde des photos terminée avec succès !",
                    "Erreur lors de la sauvegarde des photos");
}

void MainWindow::on_btnSauvegardeTotale_clicked()
{
    QString base = ui->inputPathCaseBackup->text().trimmed();
    if (base.isEmpty()) {
        base = dossierParDefaut() + "/BoiteAOutilsAndroid";
    }
    const QString destination = base + "/Sauvegarde_Totale";
    QDir().mkpath(destination);

    ui->txtLogs->setPlainText("Initialisation de la sauvegarde INTÉGRALE...\n"
                              "Destination : " + QDir::toNativeSeparators(destination) +
                              "\nCette opération peut prendre plusieurs minutes.\n");
    lancerProcessus(this, ui->txtLogs, ui->barreProgression, ui->btnSauvegardeTotale,
                    "adb", {"pull", "/sdcard/", destination},
                    "✅ Sauvegarde intégrale terminée avec succès !",
                    "Erreur lors de la sauvegarde intégrale");
}

void MainWindow::on_btnBrowseRecovery_clicked()
{
    QString fichierChoisi = QFileDialog::getOpenFileName(
        this, "Sélectionner le Recovery", dossierParDefaut(), "Fichiers Image (*.img)");
    if (!fichierChoisi.isEmpty()) {
        ui->inputPathRecovery->setText(fichierChoisi);
    }
}

void MainWindow::on_btnBrowseSystem_clicked()
{
    QString fichierChoisi = QFileDialog::getOpenFileName(
        this, "Sélectionner le Système", dossierParDefaut(), "Fichiers Image (*.img)");
    if (!fichierChoisi.isEmpty()) {
        ui->inputPathSystem->setText(fichierChoisi);
    }
}