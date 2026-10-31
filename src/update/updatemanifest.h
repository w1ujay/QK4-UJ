#ifndef UPDATEMANIFEST_H
#define UPDATEMANIFEST_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <optional>

// One downloadable file listed in update.json, keyed by platform ("windows-x64",
// "linux-flatpak-x86_64", "raspberry-pi-arm64").
struct UpdateAsset {
    QString name;
    QUrl url;
    qint64 size = 0;
    QByteArray sha256Hex;
};

// One release's changelog entry.
struct UpdateRelease {
    int uj = 0;
    QString version;
    QString date;
    QStringList added, fixed, changed;
};

struct UpdateManifest {
    int uj = 0;
    QString version;
    QHash<QString, UpdateAsset> assets;
    QList<UpdateRelease> history; // newest first
};

// Parses update.json (schema 1). Any shape problem, wrong schema or non-JSON body gives nullopt.
std::optional<UpdateManifest> parseUpdateManifest(const QByteArray &json);

#endif // UPDATEMANIFEST_H
