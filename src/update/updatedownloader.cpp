#include "updatedownloader.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

UpdateDownloader::UpdateDownloader(UpdateAsset asset, QString destinationPath, QObject *parent)
    : QObject(parent), m_asset(std::move(asset)), m_destination(std::move(destinationPath)), m_nam(this) {
    m_nam.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

UpdateDownloader::~UpdateDownloader() {
    if (!m_done)
        discard();
}

void UpdateDownloader::start() {
    m_done = false;
    m_received = 0;
    m_hash.reset();
    m_file.setFileName(m_destination + ".part");
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString msg = tr("Cannot write %1: %2").arg(m_file.fileName(), m_file.errorString());
        QTimer::singleShot(0, this, [this, msg] { fail(msg); });
        return;
    }
    QNetworkRequest req(m_asset.url);
    req.setTransferTimeout(30000); // fires only if no data moves for 30 s
    m_reply = m_nam.get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, &UpdateDownloader::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateDownloader::onFinished);
}

void UpdateDownloader::cancel() {
    if (m_done)
        return;
    m_done = true;
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
    discard();
}

void UpdateDownloader::onReadyRead() {
    if (m_done || !m_reply)
        return;
    const QByteArray chunk = m_reply->readAll();
    m_received += chunk.size();
    if (m_asset.size > 0 && m_received > m_asset.size) {
        fail(tr("Download is larger than expected (size mismatch)"));
        return;
    }
    if (m_file.write(chunk) != chunk.size()) {
        fail(tr("Cannot write %1: %2").arg(m_file.fileName(), m_file.errorString()));
        return;
    }
    m_hash.addData(chunk);
    emit progress(m_received, m_asset.size);
}

void UpdateDownloader::onFinished() {
    if (m_done || !m_reply)
        return;
    onReadyRead(); // drain anything left
    if (m_done)
        return;
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (m_reply->error() != QNetworkReply::NoError || status != 200) {
        fail(tr("Download failed: %1").arg(m_reply->errorString()));
        return;
    }
    // WHY: QFile buffers small writes, so the last bytes may still be in memory. Compare only what reached the disk.
    if (!m_file.flush()) {
        fail(tr("Cannot write %1: %2").arg(m_file.fileName(), m_file.errorString()));
        return;
    }
    if (m_received != m_asset.size) {
        fail(tr("Download size mismatch (got %1 bytes, expected %2)").arg(m_received).arg(m_asset.size));
        return;
    }
    if (m_hash.result().toHex().toLower() != m_asset.sha256Hex.toLower()) {
        fail(tr("Download checksum mismatch"));
        return;
    }
    m_file.close();
    if (m_file.error() != QFileDevice::NoError) { // close() keeps a flush error
        fail(tr("Cannot write %1: %2").arg(m_file.fileName(), m_file.errorString()));
        return;
    }
    QFile::remove(m_destination); // QFile::rename will not overwrite
    if (!m_file.rename(m_destination)) {
        fail(tr("Cannot save %1: %2").arg(m_destination, m_file.errorString()));
        return;
    }
    m_done = true;
    m_reply->deleteLater();
    emit succeeded(m_destination);
}

void UpdateDownloader::fail(const QString &message) {
    if (m_done)
        return;
    m_done = true;
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
    discard();
    emit failed(message);
}

void UpdateDownloader::discard() {
    m_file.close();
    QFile::remove(m_destination + ".part");
}
