#include "webhookmanager.h"

#include <QSettings>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QDateTime>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QUrl>

static const int MAX_QUEUE_SIZE = 500;
static const int RETRY_INTERVAL_MS = 60000;
static const int REQUEST_TIMEOUT_MS = 10000;

static bool urlValide(const QString &texte)
{
    const QUrl u(texte);
    return u.isValid() && !u.host().isEmpty()
           && (u.scheme() == "http" || u.scheme() == "https");
}

static QNetworkRequest creerRequete(const QString &url)
{
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(REQUEST_TIMEOUT_MS);
    return request;
}

static bool reponseOk(QNetworkReply *reply)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    return reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
}

// ---------------------------------------------------------------------------

WebhookManager::WebhookManager(QObject *parent)
    : QObject(parent)
{
    m_manager = new QNetworkAccessManager(this);

    QSettings settings;
    m_enabled = settings.value("webhook/enabled", false).toBool();
    m_url = settings.value("webhook/url", "").toString();

    m_retryTimer = new QTimer(this);
    connect(m_retryTimer, &QTimer::timeout, this, &WebhookManager::processQueue);
    m_retryTimer->start(RETRY_INTERVAL_MS);

    // Tentative de renvoi au démarrage (une fois la boucle d'événements lancée)
    QTimer::singleShot(0, this, &WebhookManager::processQueue);
}

void WebhookManager::setConfig(bool enabled, const QString &url)
{
    m_enabled = enabled;
    m_url = url.trimmed();

    QSettings settings;
    settings.setValue("webhook/enabled", m_enabled);
    settings.setValue("webhook/url", m_url);

    if (m_enabled) {
        processQueue();
    }
}

// ---------------------------------------------------------------------------
// Envoi : tout passe par la file (fichier), ce qui garantit l'ordre
// chronologique et évite les envois en parallèle.
// ---------------------------------------------------------------------------
void WebhookManager::sendWebhook(const QString &eventType, const QJsonObject &payload)
{
    if (!m_enabled || !urlValide(m_url)) {
        return;
    }

    QJsonObject root;
    root["event"] = eventType;
    root["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    root["payload"] = payload;

    // IMPORTANT : format Compact, sinon le JSON contient des retours à la ligne
    // et casse le format "une ligne = un événement" du fichier .jsonl
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Compact);

    appendToQueue(data);
    processQueue();
}

void WebhookManager::processQueue()
{
    if (m_isSending || !m_enabled || !urlValide(m_url)) {
        return;
    }

    const QList<QByteArray> lignes = readQueue();
    if (lignes.isEmpty()) {
        return; // rien à envoyer : aucune requête réseau
    }

    m_isSending = true;
    QNetworkReply *reply = m_manager->post(creerRequete(m_url), lignes.first());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleQueueReply(reply);
    });
}

void WebhookManager::handleQueueReply(QNetworkReply *reply)
{
    m_isSending = false; // toujours remis à false, succès ou échec

    if (reponseOk(reply)) {
        qDebug() << "Webhook envoyé avec succès";
        removeFirstFromQueue();
        reply->deleteLater();
        processQueue(); // événement suivant, s'il y en a
    } else {
        // L'événement reste dans la file, nouvel essai au prochain cycle (60 s)
        qDebug() << "Webhook : échec, nouvel essai plus tard :" << reply->errorString();
        reply->deleteLater();
    }
}

// ---------------------------------------------------------------------------
// Bouton "Tester"
// ---------------------------------------------------------------------------
void WebhookManager::testConnection(const QString &url)
{
    const QString cible = url.trimmed();
    if (!urlValide(cible)) {
        emit testFinished(false, "URL invalide : elle doit commencer par http:// ou https://");
        return;
    }

    QJsonObject root;
    root["event"] = "test";
    root["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    root["payload"] = QJsonObject{{"message", "Test de connexion"}};

    QNetworkReply *reply = m_manager->post(
        creerRequete(cible), QJsonDocument(root).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reponseOk(reply);
        QString message;
        if (ok) {
            message = QString("Connexion réussie (HTTP %1).").arg(status);
        } else {
            message = "Échec : " + reply->errorString();
            if (status == 404) {
                message += " — le workflow n8n doit être ACTIVÉ (URL /webhook/) "
                           "ou en écoute (URL /webhook-test/).";
            }
        }
        reply->deleteLater();
        emit testFinished(ok, message);
    });
}

// ---------------------------------------------------------------------------
// Fichier de file d'attente (JSON Lines)
// ---------------------------------------------------------------------------
QString WebhookManager::queueFilePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/webhook_queue.jsonl";
}

QList<QByteArray> WebhookManager::readQueue() const
{
    QList<QByteArray> resultat;
    QFile fichier(queueFilePath());
    if (!fichier.open(QIODevice::ReadOnly)) {
        return resultat;
    }
    const QList<QByteArray> lignes = fichier.readAll().split('\n');
    for (const QByteArray &ligne : lignes) {   // const : pas de détachement (clazy)
        const QByteArray propre = ligne.trimmed();
        if (!propre.isEmpty()) {
            resultat.append(propre);
        }
    }
    return resultat;
}

bool WebhookManager::writeQueue(const QList<QByteArray> &lignes) const
{
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));

    // QSaveFile : écrit dans un fichier temporaire puis remplace l'original
    // seulement si tout s'est bien passé (pas de fichier à moitié écrit)
    QSaveFile fichier(queueFilePath());
    if (!fichier.open(QIODevice::WriteOnly)) {
        qDebug() << "Impossible d'écrire la file d'attente webhook";
        return false;
    }
    for (const QByteArray &ligne : lignes) {
        fichier.write(ligne);
        fichier.write("\n");
    }
    return fichier.commit();
}

void WebhookManager::appendToQueue(const QByteArray &ligne)
{
    QList<QByteArray> lignes = readQueue();
    lignes.append(ligne);

    // Limite : on supprime les plus anciens
    while (lignes.size() > MAX_QUEUE_SIZE) {
        lignes.removeFirst();
    }
    writeQueue(lignes);
}

void WebhookManager::removeFirstFromQueue()
{
    QList<QByteArray> lignes = readQueue();
    if (!lignes.isEmpty()) {
        lignes.removeFirst();
    }
    writeQueue(lignes);
}
