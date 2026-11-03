#ifndef UPDATEDOWNLOADER_H
#define UPDATEDOWNLOADER_H

#include "updatemanifest.h"

#include <QCryptographicHash>
#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>

// Downloads one asset to a partial file unique to this run, beside the destination, hashing while writing. It moves
// to the destination only after the size and SHA-256 match the manifest. Exactly one of succeeded()/failed() is emitted
// per run, except after cancel(), which emits neither and removes the partial file. succeeded() carries the path used,
// which differs from the destination if ExistingFile::Keep had to pick a numbered name.
class UpdateDownloader : public QObject {
    Q_OBJECT
public:
    enum class ExistingFile {
        Replace, // the person agreed to it, or the name is already unique
        Keep     // never touch it: save as "2-<name>", "3-<name>" and so on
    };

    UpdateDownloader(UpdateAsset asset, QString destinationPath, ExistingFile existing, QObject *parent = nullptr);
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
    QString reserveFinalPath(QString *error) const;

    UpdateAsset m_asset;
    QString m_destination;
    ExistingFile m_existing;
    QString m_partPath;
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;
    QFile m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    qint64 m_received = 0;
    bool m_done = false;
};

#endif // UPDATEDOWNLOADER_H
