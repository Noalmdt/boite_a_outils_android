#ifndef WEBHOOKMANAGER_H
#define WEBHOOKMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QJsonObject>
#include <QList>
#include <QByteArray>
#include <QString>

class QNetworkReply;

class WebhookManager : public QObject
{
    Q_OBJECT
public:
    explicit WebhookManager(QObject *parent = nullptr);

    bool isEnabled() const { return m_enabled; }
    QString url() const { return m_url; }

    // Applique ET sauvegarde la configuration (QSettings)
    void setConfig(bool enabled, const QString &url);

    // Met l'événement dans la file (fichier) puis tente de l'envoyer
    void sendWebhook(const QString &eventType, const QJsonObject &payload);

    // Envoi d'un événement "test" direct (hors file d'attente)
    void testConnection(const QString &url);

signals:
    void testFinished(bool ok, const QString &message);

private slots:
    void processQueue();

private:
    void handleQueueReply(QNetworkReply *reply);

    QString queueFilePath() const;
    QList<QByteArray> readQueue() const;
    bool writeQueue(const QList<QByteArray> &lignes) const;
    void appendToQueue(const QByteArray &ligne);
    void removeFirstFromQueue();

    QNetworkAccessManager *m_manager = nullptr;
    QTimer *m_retryTimer = nullptr;
    QString m_url;
    bool m_enabled = false;
    bool m_isSending = false;
};

#endif // WEBHOOKMANAGER_H
