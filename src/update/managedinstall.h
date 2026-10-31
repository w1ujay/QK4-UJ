#ifndef MANAGEDINSTALL_H
#define MANAGEDINSTALL_H

#include <QString>
#include <optional>

// A launcher-managed Windows install: <root>\QK4.exe (launcher), <root>\current.txt and <root>\versions\<version>\.
struct ManagedInstall {
    QString root;
    QString version;
};

// The FileDescription in tools/windows-launcher/launcher.rc. The two must stay the same.
inline constexpr char LAUNCHER_FILE_DESCRIPTION[] = "QK4 Update Launcher";

// True when the file's Windows version resource names it as the launcher. Always false off Windows.
bool launcherHasIdentity(const QString &path);

using LauncherIdentityCheck = bool (*)(const QString &path);

// The install exePath belongs to, or nullopt when QK4 must not install in place. Writability is proved by creating
// and deleting a file, because Qt's isWritable ignores NTFS permissions by default.
std::optional<ManagedInstall> detectManagedInstall(const QString &exePath,
                                                   LauncherIdentityCheck hasIdentity = launcherHasIdentity);

// True when name is one plain path component, the launcher's rule for current.txt and version folders.
bool isSafeVersionName(const QString &name);

// The N of the version current.txt names, read as the launcher reads it. Nullopt when missing or damaged.
std::optional<int> readCurrentTxtUj(const QString &root);

#endif // MANAGEDINSTALL_H
