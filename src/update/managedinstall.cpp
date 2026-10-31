#include "update/managedinstall.h"
#include "update/updateversion.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringConverter>
#include <QStringDecoder>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winver.h>

#include <vector>
#endif

namespace {

// The launcher reads no more than this, so a longer file means the same thing to both.
constexpr qint64 CURRENT_TXT_MAX_BYTES = 65536;

bool canCreateFileIn(const QString &folder) {
    QFile probe(
        QDir(folder).filePath(QStringLiteral(".write-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!probe.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return false;
    probe.close();
    return probe.remove();
}

} // namespace

#ifdef Q_OS_WIN

bool launcherHasIdentity(const QString &path) {
    const std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(nativePath.c_str(), &ignored);
    if (size == 0)
        return false;
    std::vector<BYTE> resource(size);
    if (!GetFileVersionInfoW(nativePath.c_str(), 0, size, resource.data()))
        return false;

    struct Translation {
        WORD language;
        WORD codePage;
    };
    void *translations = nullptr;
    UINT translationBytes = 0;
    if (!VerQueryValueW(resource.data(), L"\\VarFileInfo\\Translation", &translations, &translationBytes))
        return false;

    const auto *translation = static_cast<const Translation *>(translations);
    const UINT count = translationBytes / sizeof(Translation);
    for (UINT i = 0; i < count; ++i) {
        const QString key = QStringLiteral("\\StringFileInfo\\%1%2\\FileDescription")
                                .arg(translation[i].language, 4, 16, QLatin1Char('0'))
                                .arg(translation[i].codePage, 4, 16, QLatin1Char('0'));
        void *value = nullptr;
        UINT length = 0;
        if (!VerQueryValueW(resource.data(), key.toStdWString().c_str(), &value, &length) || length == 0)
            continue;
        // The length may or may not count the terminator.
        QString description = QString::fromWCharArray(static_cast<const wchar_t *>(value), length);
        while (description.endsWith(QChar::Null))
            description.chop(1);
        if (description == QLatin1String(LAUNCHER_FILE_DESCRIPTION))
            return true;
    }
    return false;
}

#else

bool launcherHasIdentity(const QString &) {
    return false;
}

#endif

std::optional<ManagedInstall> detectManagedInstall(const QString &exePath, LauncherIdentityCheck hasIdentity) {
    QDir versions = QFileInfo(exePath).absoluteDir();
    const QString version = versions.dirName();
    if (version.isEmpty() || !versions.cdUp() ||
        versions.dirName().compare(QLatin1String("versions"), Qt::CaseInsensitive) != 0)
        return std::nullopt;
    QDir root = versions;
    if (!root.cdUp())
        return std::nullopt;

    if (!QFileInfo(root.filePath(QStringLiteral("current.txt"))).isFile())
        return std::nullopt;
    if (!hasIdentity(root.filePath(QStringLiteral("QK4.exe"))))
        return std::nullopt;
    if (!canCreateFileIn(root.path()) || !canCreateFileIn(versions.path()))
        return std::nullopt;
    return ManagedInstall{root.path(), version};
}

bool isSafeVersionName(const QString &name) {
    if (name.isEmpty() || name.contains(QLatin1String("..")))
        return false;
    return !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\')) && !name.contains(QLatin1Char(':'));
}

std::optional<int> readCurrentTxtUj(const QString &root) {
    QFile file(QDir(root).filePath(QStringLiteral("current.txt")));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray bytes = file.read(CURRENT_TXT_MAX_BYTES);

    // UTF-8, or UTF-16 with a byte order mark, because recovery means editing current.txt by hand.
    QStringDecoder decoder(QStringConverter::encodingForData(bytes).value_or(QStringConverter::Utf8));
    const QString name = QString(decoder.decode(bytes)).trimmed();
    return isSafeVersionName(name) ? UpdateVersion::parseUjNumber(name) : std::nullopt;
}
