#include "update/updatepolicy.h"

#include "update/updateversion.h"

UpdatePlatform detectPlatform(bool isWindows, bool isMacos, bool isFlatpak, const QString &cpuArch) {
    if (isWindows)
        return UpdatePlatform::Windows;
    if (isMacos)
        return UpdatePlatform::Unsupported;
    if (isFlatpak)
        return UpdatePlatform::Flatpak;
    if (cpuArch == "arm64" || cpuArch == "aarch64")
        return UpdatePlatform::PiArm64;
    if (cpuArch == "x86_64" || cpuArch == "amd64")
        return UpdatePlatform::OtherLinux;
    return UpdatePlatform::Unsupported;
}

QString assetKeyFor(UpdatePlatform platform) {
    switch (platform) {
    case UpdatePlatform::Windows:
        return QStringLiteral("windows-x64");
    case UpdatePlatform::Flatpak:
        return QStringLiteral("linux-flatpak-x86_64");
    case UpdatePlatform::PiArm64:
        return QStringLiteral("raspberry-pi-arm64");
    default:
        return QString();
    }
}

bool shouldOffer(int runningUj, std::optional<int> currentTxtUj, int offeredUj, int skippedUj,
                 const QSet<int> &failedUj) {
    if (offeredUj <= runningUj)
        return false;
    if (currentTxtUj && offeredUj <= *currentTxtUj)
        return false;
    if (offeredUj <= skippedUj)
        return false;
    return !failedUj.contains(offeredUj);
}

bool isAllowedDownloadUrl(const QUrl &url) {
    if (!url.isValid() || url.scheme() != QLatin1String("https"))
        return false;
    if (url.host() != QLatin1String("github.com") || url.port() != -1 || !url.userInfo().isEmpty())
        return false;
    const QString prefix =
        QStringLiteral("/%1/releases/download/").arg(QLatin1String(UpdateVersion::UPDATE_REPOSITORY));
    const QString path = url.path(QUrl::FullyDecoded);
    if (!path.startsWith(prefix) || path.length() == prefix.length())
        return false;
    if (path.contains(QLatin1Char('\\')))
        return false;
    const QStringList segments = path.split(QLatin1Char('/'));
    return !segments.contains(QStringLiteral("..")) && !segments.contains(QStringLiteral("."));
}

QList<UpdateRelease> releasesNewerThan(const UpdateManifest &manifest, int runningUj, bool *olderChangesMissing) {
    QList<UpdateRelease> result;
    for (const UpdateRelease &release : manifest.history) {
        if (release.uj > runningUj)
            result.append(release);
    }
    if (olderChangesMissing) {
        *olderChangesMissing =
            manifest.history.size() >= UpdatePolicy::HISTORY_LIMIT && result.size() == manifest.history.size();
    }
    return result;
}

bool validateZipEntries(const QStringList &entries, const QString &version) {
    if (version.isEmpty() || version.contains(QLatin1Char('/')) || version.contains(QLatin1Char('\\')) ||
        version == QLatin1String("..") || version == QLatin1String("."))
        return false;

    const QString versionDir = QStringLiteral("QK4/versions/%1/").arg(version);
    const QString versionExe = versionDir + QStringLiteral("QK4.exe");
    const QSet<QString> allowed = {QStringLiteral("QK4/"), QStringLiteral("QK4/QK4.exe"),
                                   QStringLiteral("QK4/current.txt"), QStringLiteral("QK4/versions/"), versionDir};
    bool hasExe = false;
    for (const QString &entry : entries) {
        if (entry.contains(QLatin1Char('\\')) || entry.contains(QLatin1Char('\0')))
            return false;
        if (entry.split(QLatin1Char('/')).contains(QStringLiteral("..")))
            return false;
        if (entry == versionExe)
            hasExe = true;
        if (allowed.contains(entry))
            continue;
        if (entry.startsWith(versionDir) && entry.length() > versionDir.length() &&
            !entry.mid(versionDir.length()).startsWith(QLatin1Char('/')))
            continue;
        return false;
    }
    return hasExe;
}
