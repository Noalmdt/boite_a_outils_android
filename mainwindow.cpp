#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "webhookmanager.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent>
#include <QSimpleUpdater.h>
#include <functional>
#include <memory>
#include <utility>

// ---------------------------------------------------------------------------
// Configuration
// La version est définie dans CMakeLists.txt (project(... VERSION x.y.z)) et
// transmise ici par target_compile_definitions : un seul endroit à modifier.
// ---------------------------------------------------------------------------
#ifndef APP_VERSION
#define APP_VERSION "2.0.0"
#endif

static const QString URL_UPDATES =
    "https://raw.githubusercontent.com/Noalmdt/boite_a_outils_android/main/updates.json";

static const int MAX_LIGNES_LOGS = 5000;            // évite de ralentir l'UI sur un gros "adb pull"
static const int MAX_SORTIE = 200000;               // caractères gardés en mémoire par flux
static const int TIMEOUT_VERIFICATION_MS = 8000;    // "fastboot devices", "getprop"...

// ---------------------------------------------------------------------------
// Utilitaires
// ---------------------------------------------------------------------------

// Retourne le chemin complet de l'outil, ou une chaîne vide s'il est introuvable.
static QString obtenirCheminExecutable(const QString &nomOutil)
{
    // 1. Dans le PATH du système
    const QString cheminTrouve = QStandardPaths::findExecutable(nomOutil);
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

    return QString();
}

// tar (bsdtar) est fourni avec Windows 10 et supérieur et sait lire les .zip.
// On préfère celui de System32 : un "tar" GNU présent dans le PATH (Git, MSYS...)
// ne sait pas ouvrir les zip.
static QString cheminTar()
{
#if defined(Q_OS_WIN)
    const QString systemRoot = qEnvironmentVariable("SystemRoot");
    if (!systemRoot.isEmpty()) {
        const QString chemin = systemRoot + "/System32/tar.exe";
        if (QFile::exists(chemin)) {
            return QDir::toNativeSeparators(chemin);
        }
    }
#endif
    return QStandardPaths::findExecutable("tar");
}

static QString dossierParDefaut()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (base.isEmpty()) {
        base = QDir::homePath();
    }
    return base;
}

static bool confirmer(QWidget *parent, const QString &titre, const QString &texte)
{
    return QMessageBox::question(parent, titre, texte,
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No) == QMessageBox::Yes;
}

static bool imageValide(const QString &chemin)
{
    const QFileInfo info(chemin);
    return !chemin.isEmpty() && info.isFile()
           && info.suffix().compare("img", Qt::CaseInsensitive) == 0;
}

static void ajouterBorne(QString &cible, const QString &texte)
{
    cible.append(texte);
    if (cible.size() > MAX_SORTIE) {
        cible.remove(0, cible.size() - MAX_SORTIE);
    }
}

// ---------------------------------------------------------------------------
// Dossiers "factory image" (GrapheneOS, images d'origine Pixel)
// ---------------------------------------------------------------------------

// Un dossier est valide s'il contient flash-all.bat, un image-*.zip et un
// bootloader-*.img. (avb_pkmd.bin n'est présent que dans GrapheneOS.)
static bool dossierFactoryValide(const QString &dossier, QString *erreur = nullptr)
{
    auto echec = [erreur](const QString &message) {
        if (erreur) *erreur = message;
        return false;
    };

    if (dossier.isEmpty()) {
        return echec("aucun dossier sélectionné.");
    }
    const QDir d(dossier);
    if (!d.exists()) {
        return echec("le dossier n'existe pas.");
    }
    if (!d.exists("flash-all.bat")) {
        return echec("flash-all.bat est introuvable dans ce dossier.");
    }
    if (d.entryList({"image-*.zip"}, QDir::Files).isEmpty()) {
        return echec("aucun fichier image-*.zip dans ce dossier.");
    }
    if (d.entryList({"bootloader-*.img"}, QDir::Files).isEmpty()) {
        return echec("aucun fichier bootloader-*.img dans ce dossier.");
    }
    return true;
}

// Si le dossier choisi n'est pas valide mais contient UN SEUL sous-dossier valide
// (cas d'un zip extrait dans un dossier parent), retourne ce sous-dossier.
static QString trouverDossierFactory(const QString &base)
{
    if (dossierFactoryValide(base)) {
        return base;
    }
    const QDir dossierBase(base);
    const QStringList sousDossiers = dossierBase.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QString trouve;
    for (const QString &nom : sousDossiers) {
        const QString chemin = dossierBase.filePath(nom);
        if (dossierFactoryValide(chemin)) {
            if (!trouve.isEmpty()) {
                return QString();   // plusieurs candidats : on ne devine pas
            }
            trouve = chemin;
        }
    }
    return trouve;
}

// ---------------------------------------------------------------------------
// Utilitaires webhook
// ---------------------------------------------------------------------------

// Libellé de commande envoyé à n8n. On garde au maximum 2 arguments :
// ça évite d'envoyer des chemins de fichiers personnels ou le code de
// déverrouillage saisi par l'utilisateur (ex : "fastboot oem unlock").
static QString libelleCommande(const QString &outil, const QStringList &arguments)
{
    return (QStringList{outil} + arguments.mid(0, 2)).join(' ');
}

static QString derniereLigne(const QString &texte)
{
    const QStringList lignes = texte.split('\n', Qt::SkipEmptyParts);
    for (int i = int(lignes.size()) - 1; i >= 0; --i) {
        const QString ligne = lignes.at(i).trimmed();
        if (!ligne.isEmpty()) {
            return ligne;
        }
    }
    return QString();
}

// Remplace les chemins personnels (dossier utilisateur, chemins "C:\...")
// avant l'envoi d'un message d'erreur à n8n.
static QString anonymiserChemins(QString texte)
{
    const QString home = QDir::homePath();
    if (!home.isEmpty()) {
        texte.replace(home, "~", Qt::CaseInsensitive);
        texte.replace(QDir::toNativeSeparators(home), "~", Qt::CaseInsensitive);
    }
    static const QRegularExpression cheminWindows("[A-Za-z]:[\\\\/][^\\s'\"]*");
    texte.replace(cheminWindows, "<chemin>");
    return texte;
}

static void signalerErreur(WebhookManager *webhook, const QString &commande, const QString &message)
{
    if (!webhook) return;
    webhook->sendWebhook("cmd_error", QJsonObject{
        {"command", commande},
        {"error_msg", anonymiserChemins(message).left(300)}
    });
}

// Analyse la sortie de "adb devices -l" ou "fastboot devices"
// et envoie un événement "device_connected" par appareil trouvé.
static void signalerAppareils(WebhookManager *webhook, const QString &sortie)
{
    if (!webhook) return;
    const QStringList lignes = sortie.split('\n', Qt::SkipEmptyParts);
    for (const QString &brute : lignes) {
        const QString ligne = brute.trimmed();
        if (ligne.startsWith("List of devices") || ligne.startsWith("*")) {
            continue;
        }
        const QStringList mots = ligne.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (mots.size() < 2) continue;

        const QString etat = mots.at(1);
        if (etat != "device" && etat != "fastboot") continue;   // ignore "unauthorized", "offline"...

        QString modele;
        for (const QString &mot : mots) {
            if (mot.startsWith("model:")) {
                modele = mot.mid(6).replace('_', ' ');
            }
        }
        if (modele.isEmpty()) {
            modele = "inconnu";
        }
        webhook->sendWebhook("device_connected", QJsonObject{
            {"serial", mots.at(0)},
            {"model", modele}
        });
    }
}

// ---------------------------------------------------------------------------
// Exécution "silencieuse" (rien dans les logs) avec délai maximum.
// Le callback reçoit (succès, sortie standard) et n'est appelé qu'UNE fois.
// ---------------------------------------------------------------------------
static void executerSilencieux(QObject *parent,
                               const QString &chemin,
                               const QStringList &arguments,
                               std::function<void(bool, const QString &)> callback,
                               int delaiMs = TIMEOUT_VERIFICATION_MS)
{
    if (chemin.isEmpty() || !QFile::exists(chemin)) {
        callback(false, QString());
        return;
    }

    auto *p = new QProcess(parent);
    auto termine = std::make_shared<bool>(false);
    auto finir = [p, termine, callback](bool ok, const QString &sortie) {
        if (*termine) return;
        *termine = true;
        p->deleteLater();
        callback(ok, sortie);
    };

    QObject::connect(p, &QProcess::finished, p,
                     [p, finir](int code, QProcess::ExitStatus statut) {
                         const QString sortie = QString::fromLocal8Bit(p->readAllStandardOutput());
                         finir(statut == QProcess::NormalExit && code == 0, sortie);
                     });
    QObject::connect(p, &QProcess::errorOccurred, p, [finir](QProcess::ProcessError erreur) {
        if (erreur == QProcess::FailedToStart) {
            finir(false, QString());
        }
    });
    QTimer::singleShot(delaiMs, p, [p, finir]() {
        if (p->state() != QProcess::NotRunning) {
            p->kill();
            finir(false, QString());
        }
    });
    p->start(chemin, arguments);
}

// Lit le modèle du téléphone (adb shell getprop ro.product.model), sans bloquer.
static void lireModeleAppareil(QObject *parent, std::function<void(const QString &)> callback)
{
    executerSilencieux(parent, obtenirCheminExecutable("adb"),
                       {"shell", "getprop", "ro.product.model"},
                       [callback](bool ok, const QString &sortie) {
                           const QString modele = derniereLigne(sortie);
                           callback((ok && !modele.isEmpty()) ? modele : QString("inconnu"));
                       });
}

// Envoie "backup_completed" : modèle du téléphone + taille du dossier de destination.
// Le calcul de la taille se fait dans un thread : l'interface ne gèle pas,
// même sur une sauvegarde intégrale de plusieurs dizaines de Go.
static void signalerSauvegarde(QObject *parent, WebhookManager *webhook, const QString &destination)
{
    if (!webhook || !webhook->isEnabled()) return;   // évite un calcul inutile

    lireModeleAppareil(parent, [parent, webhook, destination](const QString &modele) {
        auto *watcher = new QFutureWatcher<qint64>(parent);
        QObject::connect(watcher, &QFutureWatcher<qint64>::finished, parent,
                         [watcher, webhook, modele]() {
                             const qint64 octets = watcher->result();
                             webhook->sendWebhook("backup_completed", QJsonObject{
                                 {"device", modele},
                                 {"size_mb", int(octets / (1024 * 1024))},
                                 {"status", "success"}
                             });
                             watcher->deleteLater();
                         });
        watcher->setFuture(QtConcurrent::run([destination]() -> qint64 {
            qint64 octets = 0;
            QDirIterator it(destination, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                octets += it.fileInfo().size();
            }
            return octets;
        }));
    });
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
    ui->txtLogs->setMaximumBlockCount(MAX_LIGNES_LOGS);
    setWindowTitle(QString("Boîte à outils Android v%1").arg(APP_VERSION));

    // Boutons désactivés pendant une opération (jamais deux opérations en même temps)
    m_boutonsAction = {
        ui->btnDetecter, ui->btnUnlock, ui->btnRelock,
        ui->btnFlashRecovery, ui->btnFlashSystem, ui->btnFlashFactory,
        ui->btnExtraireZip, ui->btnDiagnostic,
        ui->btnSauvegarde, ui->btnSauvegardeTotale
    };
    ui->btnAnnuler->setEnabled(false);

    // Méthode de (dé)verrouillage mémorisée (0 = flashing, 1 = oem)
    QSettings settings;
    ui->comboVerrouMethode->setCurrentIndex(
        qBound(0, settings.value("bootloader/methode", 0).toInt(), 1));
    connect(ui->comboVerrouMethode, &QComboBox::currentIndexChanged, this, [](int index) {
        QSettings().setValue("bootloader/methode", index);
    });

    // Fonctions expérimentales (flash d'un dossier) : désactivées par défaut
    ui->chkExperimental->setChecked(settings.value("experimental/enabled", false).toBool());
    appliquerModeExperimental();
    connect(ui->chkExperimental, &QCheckBox::toggled, this, [this](bool actif) {
        QSettings().setValue("experimental/enabled", actif);
        appliquerModeExperimental();
    });

    // Webhook n8n : chargement de la configuration sauvegardée
    m_webhook = new WebhookManager(this);
    ui->chkWebhookEnabled->setChecked(m_webhook->isEnabled());
    ui->inputWebhookUrl->setText(m_webhook->url());

    // Sauvegarde automatique quand l'utilisateur modifie la case ou l'URL
    // (connexions faites APRÈS le chargement pour ne pas se déclencher inutilement)
    connect(ui->chkWebhookEnabled, &QCheckBox::toggled, this, [this]() { appliquerConfigWebhook(); });
    connect(ui->inputWebhookUrl, &QLineEdit::editingFinished, this, [this]() { appliquerConfigWebhook(); });
    connect(m_webhook, &WebhookManager::testFinished, this, &MainWindow::onTestWebhookFinished);

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

// ---------------------------------------------------------------------------
// Fermeture : on ne ferme jamais la fenêtre pendant un flash
// ---------------------------------------------------------------------------
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_processCourant && m_processCourant->state() != QProcess::NotRunning) {
        if (m_critique) {
            QMessageBox::warning(this, "Opération critique en cours",
                                 "Un flash ou un verrouillage est en cours.\n\n"
                                 "Fermer l'application maintenant pourrait rendre le téléphone "
                                 "inutilisable. Attendez la fin de l'opération.");
            event->ignore();
            return;
        }
        if (!confirmer(this, "Opération en cours",
                       "Une opération est en cours. L'interrompre et quitter ?")) {
            event->ignore();
            return;
        }
        m_annulee = true;
        m_processCourant->kill();
    }
    event->accept();
}

// ---------------------------------------------------------------------------
// Lance un outil (adb / fastboot / script...) sans bloquer l'interface.
// - affiche la sortie en direct dans les logs
// - désactive tous les boutons d'action pendant l'opération
// - barre de progression en mode "occupé" pendant l'exécution
// - appelle "apresSucces" (avec la sortie standard) si la commande réussit
// - envoie l'événement webhook "cmd_error" si la commande échoue
// ---------------------------------------------------------------------------
void MainWindow::lancerCommande(const Commande &c)
{
    const QString libelle = !c.libelle.isEmpty() ? c.libelle : libelleCommande(c.outil, c.arguments);
    const QString programme = QFileInfo(c.outil).isAbsolute() ? c.outil
                                                              : obtenirCheminExecutable(c.outil);

    if (programme.isEmpty() || !QFile::exists(programme)) {
        QString message = "❌ Outil introuvable : " + QFileInfo(c.outil).fileName();
        if (c.outil == "adb" || c.outil == "fastboot") {
            message += "\nPlacez le dossier platform-tools à côté de l'application "
                       "ou ajoutez-le au PATH.";
        }
        ui->txtLogs->appendPlainText(message);
        signalerErreur(m_webhook, libelle, "Outil introuvable");
        return;
    }

    auto *process = new QProcess(this);
    process->setProgram(programme);
    process->setArguments(c.arguments);
    if (!c.dossierTravail.isEmpty()) {
        process->setWorkingDirectory(c.dossierTravail);
    }
    if (c.ajouterPlatformTools) {
        // Les scripts flash-all appellent "fastboot" via le PATH : on y met
        // le dossier de NOTRE fastboot, pour utiliser une version cohérente.
        const QString cheminFastboot = obtenirCheminExecutable("fastboot");
        if (!cheminFastboot.isEmpty()) {
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("PATH", QDir::toNativeSeparators(QFileInfo(cheminFastboot).absolutePath())
                                   + QDir::listSeparator() + env.value("PATH"));
            process->setProcessEnvironment(env);
        }
    }

    struct Etat {
        QString sortie;    // stdout
        QString erreurs;   // stderr (fastboot y écrit sa progression normale)
    };
    auto etat = std::make_shared<Etat>();

    m_processCourant = process;
    m_critique = c.critique;
    m_annulee = false;
    ui->barreProgression->setRange(0, 0);   // mode "occupé"
    definirOccupe(true);

    connect(process, &QProcess::readyReadStandardOutput, process, [this, process, etat]() {
        const QString texte = QString::fromLocal8Bit(process->readAllStandardOutput());
        ajouterBorne(etat->sortie, texte);
        const QString propre = texte.trimmed();
        if (!propre.isEmpty()) {
            ui->txtLogs->appendPlainText(propre);
        }
    });
    connect(process, &QProcess::readyReadStandardError, process, [this, process, etat]() {
        const QString texte = QString::fromLocal8Bit(process->readAllStandardError());
        ajouterBorne(etat->erreurs, texte);
        const QString propre = texte.trimmed();
        if (!propre.isEmpty()) {
            ui->txtLogs->appendPlainText(propre);
        }
    });

    if (c.fermerEntree) {
        connect(process, &QProcess::started, process, [process]() { process->closeWriteChannel(); });
    }

    connect(process, &QProcess::errorOccurred, process,
            [this, process, libelle, programme](QProcess::ProcessError erreur) {
                if (erreur != QProcess::FailedToStart) return;
                terminerOperation(0);
                ui->txtLogs->appendPlainText("❌ Impossible de démarrer : " + programme);
                signalerErreur(m_webhook, libelle, "Impossible de démarrer l'outil");
                process->deleteLater();
            });

    connect(process, &QProcess::finished, process,
            [this, process, etat, c, libelle](int exitCode, QProcess::ExitStatus exitStatus) {
                const bool annulee = m_annulee;
                m_annulee = false;
                // On libère l'interface AVANT apresSucces, qui peut lancer une autre commande
                terminerOperation(annulee ? 0 : 100);

                bool succes = (exitStatus == QProcess::NormalExit && exitCode == 0);
                QString detail;

                // Un script .bat peut renvoyer 0 alors qu'une commande fastboot a échoué
                if (succes && c.verifierSortie) {
                    static const QRegularExpression motifEchec("FAILED|fastboot: error");
                    // Lignes connues comme bénignes : l'effacement d'une clé AVB personnalisée
                    // échoue normalement quand aucune clé n'est enregistrée (GrapheneOS).
                    static const QRegularExpression motifBenin("avb_custom_key",
                                                              QRegularExpression::CaseInsensitiveOption);
                    QStringList lignesUtiles;
                    const QStringList toutes = (etat->sortie + "\n" + etat->erreurs).split('\n');
                    for (const QString &ligne : toutes) {
                        if (!motifBenin.match(ligne).hasMatch()) {
                            lignesUtiles.append(ligne);
                        }
                    }
                    if (motifEchec.match(lignesUtiles.join('\n')).hasMatch()) {
                        succes = false;
                        detail = "Le script s'est terminé mais a signalé une erreur (FAILED)";
                    }
                }

                if (annulee) {
                    ui->txtLogs->appendPlainText("\n⏹ Opération annulée.");
                } else if (succes) {
                    if (!c.messageSucces.isEmpty()) {
                        ui->txtLogs->appendPlainText("\n" + c.messageSucces);
                    }
                    if (c.apresSucces) {
                        c.apresSucces(etat->sortie);
                    }
                } else {
                    ui->txtLogs->appendPlainText("\n❌ " + c.messageEchec
                                                 + " (code : " + QString::number(exitCode) + ")");
                    if (detail.isEmpty()) detail = derniereLigne(etat->erreurs);
                    if (detail.isEmpty()) detail = derniereLigne(etat->sortie);
                    if (exitStatus == QProcess::CrashExit) detail = "Le processus s'est arrêté brutalement";
                    if (detail.isEmpty()) detail = "Code de sortie " + QString::number(exitCode);
                    signalerErreur(m_webhook, libelle, detail);
                }
                process->deleteLater();
            });

    process->start();
}

void MainWindow::terminerOperation(int valeurBarre)
{
    ui->barreProgression->setRange(0, 100);
    ui->barreProgression->setValue(valeurBarre);
    m_critique = false;
    m_processCourant.clear();
    definirOccupe(false);
}

void MainWindow::definirOccupe(bool occupe)
{
    m_occupe = occupe;
    for (QWidget *bouton : std::as_const(m_boutonsAction)) {
        bouton->setEnabled(!occupe);
    }
    // Les fonctions expérimentales restent grisées tant que la case n'est pas cochée
    if (!ui->chkExperimental->isChecked()) {
        ui->btnFlashFactory->setEnabled(false);
        ui->btnExtraireZip->setEnabled(false);
    }
    // Annulation possible seulement pour un vrai processus non critique
    ui->btnAnnuler->setEnabled(occupe && !m_processCourant.isNull() && !m_critique);
}

void MainWindow::appliquerModeExperimental()
{
    const bool actif = ui->chkExperimental->isChecked();
    ui->lblFactoryInfo->setEnabled(actif);
    ui->inputPathFactory->setEnabled(actif);
    ui->btnBrowseFactory->setEnabled(actif);
    definirOccupe(m_occupe);   // réapplique l'état des boutons
}

// Vérifie (sans bloquer) qu'un appareil est visible en mode fastboot,
// puis appelle "suite". Sinon, explique quoi faire.
void MainWindow::verifierFastbootPuis(std::function<void()> suite)
{
    if (m_occupe) return;

    const QString chemin = obtenirCheminExecutable("fastboot");
    if (chemin.isEmpty()) {
        ui->txtLogs->appendPlainText("❌ Outil introuvable : fastboot\n"
                                     "Placez le dossier platform-tools à côté de l'application "
                                     "ou ajoutez-le au PATH.");
        signalerErreur(m_webhook, "fastboot devices", "Outil introuvable");
        return;
    }

    ui->txtLogs->appendPlainText("🔎 Recherche d'un appareil en mode fastboot...");
    ui->barreProgression->setRange(0, 0);
    definirOccupe(true);

    executerSilencieux(this, chemin, {"devices"},
                       [this, suite](bool ok, const QString &sortie) {
                           ui->barreProgression->setRange(0, 100);
                           ui->barreProgression->setValue(0);
                           definirOccupe(false);

                           static const QRegularExpression motif("\\sfastboot");
                           if (!ok || !sortie.contains(motif)) {
                               ui->txtLogs->appendPlainText(
                                   "❌ Aucun appareil détecté en mode fastboot.\n"
                                   "Démarrez le téléphone en mode bootloader (fastboot), "
                                   "branchez-le directement au PC puis réessayez.");
                               return;
                           }
                           suite();
                       });
}

// ---------------------------------------------------------------------------
// Détection / diagnostic
// ---------------------------------------------------------------------------
void MainWindow::on_btnDetecter_clicked()
{
    ui->txtLogs->setPlainText("Détection de l'appareil en cours...\n");

    Commande c;
    c.outil = "fastboot";
    c.arguments = {"devices"};
    c.messageSucces = "Détection terminée.";
    c.messageEchec = "Échec de la détection";
    c.apresSucces = [this](const QString &sortie) { signalerAppareils(m_webhook, sortie); };
    lancerCommande(c);
}

void MainWindow::on_btnDiagnostic_clicked()
{
    ui->txtLogs->setPlainText("Exécution du diagnostic...\n\n--- APPAREILS EN LIGNE ---\n");

    // Première commande : liste des appareils (avec le modèle grâce à -l), puis état de la batterie
    Commande c;
    c.outil = "adb";
    c.arguments = {"devices", "-l"};
    c.messageEchec = "Échec de la commande adb devices";
    c.apresSucces = [this](const QString &sortie) {
        signalerAppareils(m_webhook, sortie);

        ui->txtLogs->appendPlainText("\n--- ÉTAT DE LA BATTERIE ---");
        Commande batterie;
        batterie.outil = "adb";
        batterie.arguments = {"shell", "dumpsys", "battery"};
        batterie.messageSucces = "Diagnostic terminé.";
        batterie.messageEchec = "Échec de la lecture de la batterie";
        lancerCommande(batterie);
    };
    lancerCommande(c);
}

// ---------------------------------------------------------------------------
// Bootloader : déverrouillage / reverrouillage
// ---------------------------------------------------------------------------
void MainWindow::on_btnUnlock_clicked()
{
    const bool methodeFlashing = (ui->comboVerrouMethode->currentIndex() == 0);
    const QString codeSecret = ui->inputCodeSecret->text().trimmed();

    verifierFastbootPuis([this, methodeFlashing, codeSecret]() {
        if (!confirmer(this, "ATTENTION - ZONE RISQUÉE",
                       "Le déverrouillage va EFFACER toutes vos données. Voulez-vous continuer ?")) {
            ui->txtLogs->setPlainText("Opération annulée par sécurité.");
            return;
        }

        QStringList arguments = methodeFlashing ? QStringList{"flashing", "unlock"}
                                                : QStringList{"oem", "unlock"};
        QString note;
        if (!codeSecret.isEmpty()) {
            if (methodeFlashing) {
                note = "ℹ️ Le code saisi est ignoré avec la méthode « flashing ».\n";
            } else {
                arguments.append(codeSecret);
            }
        }

        ui->txtLogs->setPlainText(note + "Envoi de la commande de déverrouillage...\n"
                                  "(Confirmez sur l'écran du téléphone si on vous le demande.)\n");
        Commande c;
        c.outil = "fastboot";
        c.arguments = arguments;
        c.messageSucces = "Commande de déverrouillage envoyée.";
        c.messageEchec = "Échec du déverrouillage";
        lancerCommande(c);
    });
}

void MainWindow::on_btnRelock_clicked()
{
    const bool methodeFlashing = (ui->comboVerrouMethode->currentIndex() == 0);

    verifierFastbootPuis([this, methodeFlashing]() {
        if (!confirmer(this, "ATTENTION - DANGER",
                       "Reverrouiller le bootloader EFFACE toutes les données.\n\n"
                       "Ne reverrouillez QUE si le système installé est signé et compatible "
                       "avec le verrouillage (GrapheneOS, système d'origine du fabricant). "
                       "Sinon, le téléphone peut ne plus démarrer.\n\n"
                       "Êtes-vous ABSOLUMENT sûr ?")) {
            ui->txtLogs->setPlainText("Reverrouillage annulé.");
            return;
        }

        ui->txtLogs->setPlainText("Reverrouillage du bootloader...\n"
                                  "(Confirmez sur l'écran du téléphone si on vous le demande.)\n");
        Commande c;
        c.outil = "fastboot";
        c.arguments = methodeFlashing ? QStringList{"flashing", "lock"} : QStringList{"oem", "lock"};
        c.messageSucces = "Commande de reverrouillage envoyée.";
        c.messageEchec = "Échec du reverrouillage";
        c.critique = true;
        lancerCommande(c);
    });
}

// ---------------------------------------------------------------------------
// Flash d'une image (.img)
// ---------------------------------------------------------------------------
void MainWindow::on_btnFlashRecovery_clicked()
{
    const QString fichier = ui->inputPathRecovery->text().trimmed();
    if (!imageValide(fichier)) {
        ui->txtLogs->setPlainText("Erreur : veuillez sélectionner un fichier .img valide avant de flasher !");
        return;
    }

    verifierFastbootPuis([this, fichier]() {
        if (!confirmer(this, "ATTENTION - ZONE RISQUÉE",
                       "Vous allez modifier la partition Recovery avec le fichier :\n\n"
                       + QFileInfo(fichier).fileName() + "\n\nVoulez-vous continuer ?")) {
            ui->txtLogs->setPlainText("Flashage du Recovery annulé.");
            return;
        }

        ui->txtLogs->setPlainText("Début du flashage Recovery...\nFichier : " + fichier + "\n");
        Commande c;
        c.outil = "fastboot";
        c.arguments = {"flash", "recovery", fichier};
        c.messageSucces = "✅ Flash du Recovery terminé avec succès !";
        c.messageEchec = "Échec du flashage. Vérifiez la connexion du téléphone";
        c.critique = true;
        c.apresSucces = [this](const QString &) {
            m_webhook->sendWebhook("flash_completed", QJsonObject{
                {"partition", "recovery"},
                {"status", "success"}
            });
        };
        lancerCommande(c);
    });
}

void MainWindow::on_btnFlashSystem_clicked()
{
    const QString fichier = ui->inputPathSystem->text().trimmed();
    if (!imageValide(fichier)) {
        ui->txtLogs->setPlainText("Erreur : veuillez sélectionner un fichier .img valide avant de flasher !");
        return;
    }

    verifierFastbootPuis([this, fichier]() {
        if (!confirmer(this, "ATTENTION - DANGER MAXIMUM",
                       "Vous allez écraser le système d'exploitation actuel du téléphone avec :\n\n"
                       + QFileInfo(fichier).fileName() + "\n\nÊtes-vous ABSOLUMENT sûr ?")) {
            ui->txtLogs->setPlainText("Flashage du Système annulé.");
            return;
        }

        ui->txtLogs->setPlainText("⚠️ FLASHAGE SYSTÈME EN COURS...\n"
                                  "Cette opération est longue. Ne débranchez sous aucun prétexte le téléphone.\n");
        Commande c;
        c.outil = "fastboot";
        c.arguments = {"flash", "system", fichier};
        c.messageSucces = "✅ Félicitations ! Le système a été flashé avec succès.";
        c.messageEchec = "ERREUR CRITIQUE : le flashage du système a échoué. "
                         "Vérifiez le câble, le port USB, et assurez-vous que le bootloader est déverrouillé";
        c.critique = true;
        c.apresSucces = [this](const QString &) {
            m_webhook->sendWebhook("flash_completed", QJsonObject{
                {"partition", "system"},
                {"status", "success"}
            });
        };
        lancerCommande(c);
    });
}

// ---------------------------------------------------------------------------
// Flash d'un dossier (GrapheneOS, factory image Pixel) via flash-all.bat
// ---------------------------------------------------------------------------
void MainWindow::on_btnExtraireZip_clicked()
{
    const QString zip = QFileDialog::getOpenFileName(
        this, "Sélectionner le zip de l'image (GrapheneOS / factory image)",
        dossierParDefaut(), "Archives ZIP (*.zip)");
    if (zip.isEmpty()) {
        return;
    }

    const QString tar = cheminTar();
    if (tar.isEmpty()) {
        ui->txtLogs->setPlainText("❌ L'outil tar est introuvable (Windows 10 ou supérieur requis).\n"
                                  "Extrayez le zip manuellement, puis sélectionnez le dossier.");
        return;
    }

    const QFileInfo info(zip);
    const QString destination = info.absolutePath() + "/" + info.completeBaseName() + "_extrait";
    QDir().mkpath(destination);

    ui->txtLogs->setPlainText("Extraction de " + info.fileName() + "...\n"
                              "Destination : " + QDir::toNativeSeparators(destination) +
                              "\nCela peut prendre quelques minutes.\n");
    Commande c;
    c.outil = tar;
    c.arguments = {"-xf", QDir::toNativeSeparators(zip), "-C", QDir::toNativeSeparators(destination)};
    c.libelle = "tar -xf";   // évite d'envoyer des chemins au webhook
    c.messageSucces = "✅ Extraction terminée.";
    c.messageEchec = "Échec de l'extraction";
    c.apresSucces = [this, destination](const QString &) {
        const QString dossier = trouverDossierFactory(destination);
        if (dossier.isEmpty()) {
            ui->inputPathFactory->setText(destination);
            ui->txtLogs->appendPlainText("⚠️ Aucun dossier de factory image reconnu dans l'archive. "
                                         "Vérifiez que c'est bien le zip de votre appareil.");
        } else {
            ui->inputPathFactory->setText(dossier);
            ui->txtLogs->appendPlainText("📁 Dossier prêt : " + QDir::toNativeSeparators(dossier));
        }
    };
    lancerCommande(c);
}

void MainWindow::on_btnFlashFactory_clicked()
{
    if (!ui->chkExperimental->isChecked()) {
        return;   // sécurité : la fonction n'est utilisable que si les fonctions expérimentales sont activées
    }
    const QString dossier = ui->inputPathFactory->text().trimmed();

    QString erreur;
    if (!dossierFactoryValide(dossier, &erreur)) {
        ui->txtLogs->setPlainText("Erreur : dossier invalide (" + erreur + ")\n"
                                  "Sélectionnez le dossier extrait qui contient flash-all.bat.");
        return;
    }

    const bool graphene = QFile::exists(QDir(dossier).filePath("avb_pkmd.bin"));
    const QString nomDossier = QFileInfo(dossier).fileName();

    verifierFastbootPuis([this, dossier, graphene, nomDossier]() {
        const QString type = graphene ? "GrapheneOS" : "factory image, système d'origine";
        if (!confirmer(this, "ATTENTION - DANGER MAXIMUM",
                       "Vous allez flasher « " + nomDossier + " » (" + type + ").\n\n"
                       "⚠ FONCTION EXPÉRIMENTALE : non testée sur appareil réel par l'auteur.\n"
                       "Le guide officiel de votre système (ex. installeur web GrapheneOS) reste "
                       "la méthode de référence.\n\n"
                       "• Toutes les données seront EFFACÉES\n"
                       "• Le bootloader doit être déverrouillé\n"
                       "• Le dossier doit correspondre EXACTEMENT au modèle du téléphone\n"
                       "• Ne débranchez pas le téléphone avant la fin\n\n"
                       "Êtes-vous ABSOLUMENT sûr ?")) {
            ui->txtLogs->setPlainText("Flashage du dossier annulé.");
            return;
        }

        ui->txtLogs->setPlainText("⚠️ FLASHAGE EN COURS : " + nomDossier + " (" + type + ")\n"
                                  "Cette opération est longue (plusieurs minutes) et le téléphone "
                                  "peut redémarrer plusieurs fois. Ne débranchez rien.\n");

        Commande c;
        c.outil = "cmd";
        c.arguments = {"/c", "flash-all.bat"};
        c.dossierTravail = dossier;
        c.libelle = "flash-all.bat";
        c.critique = true;
        c.ajouterPlatformTools = true;
        c.fermerEntree = true;
        c.verifierSortie = true;
        c.messageSucces = "✅ Le script de flash s'est terminé sans erreur.\n"
                          "Étape suivante : reverrouillez le bootloader (bouton « Reverrouiller le "
                          "Bootloader ») en suivant le guide officiel de votre système, "
                          "puis laissez le téléphone démarrer.";
        c.messageEchec = "ERREUR CRITIQUE : le flash a échoué. Ne redémarrez pas à la légère : "
                         "lisez le journal, vérifiez le câble, le port USB, la version de fastboot "
                         "et que le bootloader est déverrouillé";
        c.apresSucces = [this, nomDossier](const QString &) {
            m_webhook->sendWebhook("flash_completed", QJsonObject{
                {"partition", "factory_image"},
                {"image", nomDossier},
                {"status", "success"}
            });
        };
        lancerCommande(c);
    });
}

// ---------------------------------------------------------------------------
// Sauvegardes
// ---------------------------------------------------------------------------
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
    Commande c;
    c.outil = "adb";
    c.arguments = {"pull", "/sdcard/DCIM/Camera/", destination};
    c.messageSucces = "✅ Sauvegarde des photos terminée avec succès !";
    c.messageEchec = "Erreur lors de la sauvegarde des photos";
    c.apresSucces = [this, destination](const QString &) {
        signalerSauvegarde(this, m_webhook, destination);
    };
    lancerCommande(c);
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
    Commande c;
    c.outil = "adb";
    c.arguments = {"pull", "/sdcard/", destination};
    c.messageSucces = "✅ Sauvegarde intégrale terminée avec succès !";
    c.messageEchec = "Erreur lors de la sauvegarde intégrale";
    c.apresSucces = [this, destination](const QString &) {
        signalerSauvegarde(this, m_webhook, destination);
    };
    lancerCommande(c);
}

// ---------------------------------------------------------------------------
// Sélection de fichiers / dossiers
// ---------------------------------------------------------------------------
void MainWindow::on_btnBrowseRecovery_clicked()
{
    const QString fichierChoisi = QFileDialog::getOpenFileName(
        this, "Sélectionner le Recovery", dossierParDefaut(), "Fichiers Image (*.img)");
    if (!fichierChoisi.isEmpty()) {
        ui->inputPathRecovery->setText(fichierChoisi);
    }
}

void MainWindow::on_btnBrowseSystem_clicked()
{
    const QString fichierChoisi = QFileDialog::getOpenFileName(
        this, "Sélectionner le Système", dossierParDefaut(), "Fichiers Image (*.img)");
    if (!fichierChoisi.isEmpty()) {
        ui->inputPathSystem->setText(fichierChoisi);
    }
}

void MainWindow::on_btnBrowseFactory_clicked()
{
    const QString dossierChoisi = QFileDialog::getExistingDirectory(
        this, "Sélectionner le dossier extrait (contient flash-all.bat)", dossierParDefaut());
    if (dossierChoisi.isEmpty()) {
        return;
    }

    const QString valide = trouverDossierFactory(dossierChoisi);
    if (valide.isEmpty()) {
        ui->inputPathFactory->setText(dossierChoisi);
        ui->txtLogs->appendPlainText("⚠️ Ce dossier ne ressemble pas à une factory image "
                                     "(flash-all.bat, image-*.zip et bootloader-*.img attendus).");
    } else {
        ui->inputPathFactory->setText(valide);
        if (valide != dossierChoisi) {
            ui->txtLogs->appendPlainText("📁 Sous-dossier détecté : " + QDir::toNativeSeparators(valide));
        }
    }
}

void MainWindow::on_btnBrowseCaseBackup_clicked()
{
    const QString dossierChoisi = QFileDialog::getExistingDirectory(
        this, "Sélectionner le dossier de sauvegarde", dossierParDefaut());
    if (!dossierChoisi.isEmpty()) {
        ui->inputPathCaseBackup->setText(dossierChoisi);
    }
}

// ---------------------------------------------------------------------------
// Annulation (jamais pendant un flash)
// ---------------------------------------------------------------------------
void MainWindow::on_btnAnnuler_clicked()
{
    if (m_processCourant.isNull() || m_critique) {
        return;
    }
    ui->txtLogs->appendPlainText("\n⏳ Annulation en cours...");
    m_annulee = true;
    m_processCourant->kill();
}

// ---------------------------------------------------------------------------
// Webhook n8n
// ---------------------------------------------------------------------------
void MainWindow::appliquerConfigWebhook()
{
    m_webhook->setConfig(ui->chkWebhookEnabled->isChecked(), ui->inputWebhookUrl->text());
    if (ui->chkWebhookEnabled->isChecked() && ui->inputWebhookUrl->text().trimmed().isEmpty()) {
        ui->txtLogs->appendPlainText("⚠️ Webhooks activés mais aucune URL renseignée : rien ne sera envoyé.");
    }
}

void MainWindow::on_btnTestWebhook_clicked()
{
    const QString url = ui->inputWebhookUrl->text().trimmed();
    if (url.isEmpty()) {
        ui->txtLogs->appendPlainText("❌ Veuillez entrer une URL.");
        return;
    }
    ui->btnTestWebhook->setEnabled(false);
    ui->txtLogs->appendPlainText("⏳ Test de connexion vers : " + url);
    m_webhook->testConnection(url);
}

void MainWindow::onTestWebhookFinished(bool ok, const QString &message)
{
    ui->btnTestWebhook->setEnabled(true);
    ui->txtLogs->appendPlainText((ok ? "✅ " : "❌ ") + message);
}
