#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include "updatemanifest.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <optional>

// Fetches and parses update.json. Emits finished() exactly once: the manifest, or nullopt on any
// failure (network error, non-200 status, bad body, or no answer within 5 seconds).
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QUrl url, QObject *parent = nullptr);

    void start();

signals:
    void finished(std::optional<UpdateManifest> manifest);

private:
    void finish(std::optional<UpdateManifest> manifest);

    QUrl m_url;
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;
    QTimer m_guard;
    bool m_done = false;
};

#endif // UPDATECHECKER_H
