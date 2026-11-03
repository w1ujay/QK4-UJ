#ifndef WINDOWSAPPID_H
#define WINDOWSAPPID_H

#include <QString>

class QWidget;

// Points the window's taskbar identity at the launcher, so a taskbar pin survives cleanup removing the version folder
// the pinned exe was running from. Does nothing off Windows, and nothing on failure beyond a log line: a pin that ties
// to one version must never stop QK4 starting.
void applyWindowsRelaunchIdentity(QWidget *window, const QString &launcherPath);

#endif // WINDOWSAPPID_H
