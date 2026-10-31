#ifndef UPDATEDOWNLOADER_H
#define UPDATEDOWNLOADER_H

#include "updatemanifest.h"

#include <QCryptographicHash>
#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>

// Downloads one asset to "<destination>.part", hashing while writing, and renames to the destination only
// after the size and SHA-256 match the manifest. Exactly one of succeeded()/failed() is emitted per run,
// except after cancel(), which emits neither and removes the partial file.
class UpdateDownloader : public QObject {
    Q_OBJECT
public:
    UpdateDownloader(UpdateAsset asset, QString destinationPath, QObject *parent = nullptr);
    ~UpdateDownloader() override;

    void start();
    void cancel();

signals:
    void progress(qint64 received, qint64 total);
    void succeeded(QString path);
    void failed(QString message);

private:
    void onReadyRead();
    void onFinished();
    void fail(const QString &message);
    void discard();

    UpdateAsset m_asset;
    QString m_destination;
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;
    QFile m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    qint64 m_received = 0;
    bool m_done = false;
};

#endif // UPDATEDOWNLOADER_H
