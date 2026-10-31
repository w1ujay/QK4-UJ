#include "updatedownloader.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUuid>

#include <filesystem>
#include <system_error>

namespace {

// WHY not QFile::rename: it refuses to replace a file, and if a rename fails it falls back to copying over the target.
bool moveOver(const QString &from, const QString &to, QString *error) {
    std::error_code ec;
    std::filesystem::rename(std::filesystem::path(from.toStdU16String()), std::filesystem::path(to.toStdU16String()),
                            ec);
    if (ec)
        *error = QString::fromStdString(ec.message());
    return !ec;
}

} // namespace

UpdateDownloader::UpdateDownloader(UpdateAsset asset, QString destinationPath, ExistingFile existing, QObject *parent)
    : QObject(parent), m_asset(std::move(asset)), m_destination(std::move(destinationPath)), m_existing(existing),
      m_nam(this) {
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
    // WHY unique: another copy can be downloading to the same destination, and its partial file is not ours to touch.
    // Same folder as the destination, so the final move stays on one volume.
    m_partPath = QStringLiteral("%1.%2.part").arg(m_destination, QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_file.setFileName(m_partPath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
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
    QString saveError;
    const QString saved = reserveFinalPath(&saveError);
    if (saved.isEmpty()) {
        fail(tr("Cannot save %1: %2").arg(m_destination, saveError));
        return;
    }
    if (!moveOver(m_partPath, saved, &saveError)) {
        if (m_existing == ExistingFile::Keep)
            QFile::remove(saved); // only the empty placeholder this run created
        fail(tr("Cannot save %1: %2").arg(saved, saveError));
        return;
    }
    m_done = true;
    m_reply->deleteLater();
    emit succeeded(saved);
}

// For Keep, this claims the name by creating an empty file, so no other copy can take it before the move replaces it.
QString UpdateDownloader::reserveFinalPath(QString *error) const {
    if (m_existing == ExistingFile::Replace)
        return m_destination;
    const QFileInfo target(m_destination);
    QString path = m_destination;
    for (int i = 2;; ++i) {
        QFile placeholder(path);
        if (placeholder.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return path;
        if (!QFileInfo::exists(path)) { // refused for a reason other than the name being taken
            *error = placeholder.errorString();
            return {};
        }
        path = target.dir().filePath(QStringLiteral("%1-%2").arg(i).arg(target.fileName()));
    }
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
    if (!m_partPath.isEmpty())
        QFile::remove(m_partPath);
}
