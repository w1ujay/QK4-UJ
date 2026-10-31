#include "updatechecker.h"

#include <QNetworkReply>
#include <QNetworkRequest>

namespace {
constexpr int CHECK_TIMEOUT_MS = 5000;
}

UpdateChecker::UpdateChecker(QUrl url, QObject *parent) : QObject(parent), m_url(std::move(url)), m_nam(this) {
    m_nam.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    m_guard.setSingleShot(true);
    m_guard.setInterval(CHECK_TIMEOUT_MS);
    connect(&m_guard, &QTimer::timeout, this, [this] {
        if (m_reply)
            m_reply->abort(); // its finished handler is ignored once m_done is set
        finish(std::nullopt);
    });
}

void UpdateChecker::start() {
    m_done = false;
    QNetworkRequest req(m_url);
    req.setTransferTimeout(CHECK_TIMEOUT_MS);
    req.setRawHeader("Accept", "application/json");
    m_reply = m_nam.get(req);
    m_guard.start();
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply] {
        reply->deleteLater();
        if (m_done)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            finish(std::nullopt);
            return;
        }
        finish(parseUpdateManifest(reply->readAll()));
    });
}

void UpdateChecker::finish(std::optional<UpdateManifest> manifest) {
    if (m_done)
        return;
    m_done = true;
    m_guard.stop();
    emit finished(std::move(manifest));
}
