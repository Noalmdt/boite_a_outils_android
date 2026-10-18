#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QList>
#include <QMainWindow>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <functional>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QCloseEvent;
class QWidget;
class WebhookManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void on_btnDetecter_clicked();
    void on_btnUnlock_clicked();
    void on_btnRelock_clicked();
    void on_btnFlashRecovery_clicked();
    void on_btnFlashSystem_clicked();
    void on_btnFlashFactory_clicked();
    void on_btnExtraireZip_clicked();
    void on_btnDiagnostic_clicked();
    void on_btnSauvegarde_clicked();
    void on_btnSauvegardeTotale_clicked();
    void on_btnBrowseRecovery_clicked();
    void on_btnBrowseSystem_clicked();
    void on_btnBrowseFactory_clicked();
    void on_btnBrowseCaseBackup_clicked();
    void on_btnAnnuler_clicked();
    void on_btnTestWebhook_clicked();

private:
    // Les variables et fonctions ordinaires vont ICI, jamais sous
    // "private slots:" (sinon moc : "Not a signal or slot declaration")

    // Description d'une commande à lancer (adb, fastboot, script, tar...)
    struct Commande {
        QString outil;                 // "adb", "fastboot", "cmd"... ou chemin absolu
        QStringList arguments;
        QString messageSucces;
        QString messageEchec;
        std::function<void(const QString &)> apresSucces;   // reçoit la sortie standard
        QString libelle;               // libellé envoyé au webhook (sinon calculé)
        QString dossierTravail;        // optionnel
        bool critique = false;         // flash : ni annulation, ni fermeture de la fenêtre
        bool ajouterPlatformTools = false;  // ajoute le dossier de fastboot au PATH du processus
        bool fermerEntree = false;     // ferme stdin (évite qu'un "pause" bloque un script .bat)
        bool verifierSortie = false;   // un "FAILED" dans la sortie = échec, même si le code est 0
    };

    void lancerCommande(const Commande &commande);
    void terminerOperation(int valeurBarre);
    void definirOccupe(bool occupe);
    void verifierFastbootPuis(std::function<void()> suite);

    void appliquerModeExperimental();
    void appliquerConfigWebhook();
    void onTestWebhookFinished(bool ok, const QString &message);

    Ui::MainWindow *ui;
    WebhookManager *m_webhook = nullptr;

    QList<QWidget *> m_boutonsAction;      // boutons désactivés pendant une opération
    QPointer<QProcess> m_processCourant;   // processus en cours (le cas échéant)
    bool m_occupe = false;
    bool m_critique = false;               // l'opération en cours est un flash
    bool m_annulee = false;                // l'utilisateur a demandé l'annulation
};
#endif // MAINWINDOW_H
