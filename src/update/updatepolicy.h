#ifndef UPDATEPOLICY_H
#define UPDATEPOLICY_H

#include "update/updatemanifest.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <optional>

// Which kind of install is running, as far as updating is concerned.
enum class UpdatePlatform { Windows, Flatpak, PiArm64, OtherLinux, Unsupported };

namespace UpdatePolicy {
// update.json keeps this many releases in `history`.
inline constexpr int HISTORY_LIMIT = 20;
} // namespace UpdatePolicy

// Flatpak wins over the CPU architecture. macOS and anything unrecognised is Unsupported.
UpdatePlatform detectPlatform(bool isWindows, bool isMacos, bool isFlatpak, const QString &cpuArch);

// The update.json assets key for a platform, or empty when it has none.
QString assetKeyFor(UpdatePlatform platform);

// True when the offered build is newer than what is running, what current.txt already names, the skipped
// version, and has not failed before. Pass skippedUj <= 0 when nothing was skipped.
bool shouldOffer(int runningUj, std::optional<int> currentTxtUj, int offeredUj, int skippedUj,
                 const QSet<int> &failedUj);

// Only https URLs under the fork's releases/download/ path are accepted.
bool isAllowedDownloadUrl(const QUrl &url);

// History entries newer than runningUj, newest first. olderChangesMissing (may be null) is set when the
// history window is full and every entry in it is newer, so even older changes may exist.
QList<UpdateRelease> releasesNewerThan(const UpdateManifest &manifest, int runningUj, bool *olderChangesMissing);

// True when a Windows package's zip entries are exactly the launcher layout for `version`.
bool validateZipEntries(const QStringList &entries, const QString &version);

#endif // UPDATEPOLICY_H
