#ifndef UPDATEDIALOG_H
#define UPDATEDIALOG_H

#include "update/updatemanifest.h"

#include <QDialog>
#include <QList>
#include <QString>

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

// The "update available" popup: title, versions, plain-text changelog, then the choices. The owner
// connects the signals and drives the later states (progress, error, manual steps) through the slots.
class UpdateDialog : public QDialog {
    Q_OBJECT

public:
    UpdateDialog(QString currentVersion, QString newVersion, QList<UpdateRelease> releases, bool olderChangesMissing,
                 bool canInstall, QWidget *parent);

    // Plain-text changelog for the releases, newest first. Public so the owner and tests share one format.
    static QString changelogText(const QList<UpdateRelease> &releases, bool olderChangesMissing);

signals:
    void installRequested();
    void laterRequested();
    void skipRequested();
    void cancelRequested();
    void retryRequested();

public slots:
    // total <= 0 shows a busy bar (work of unknown length).
    void showProgress(qint64 done, qint64 total);
    void showError(const QString &message);
    // copyText empty: no Copy button.
    void showManualSteps(const QString &text, const QString &copyText);
    void showIdleButtons();

protected:
    // Esc and the window close button mean Later, or Cancel while a download is running.
    void reject() override;

private:
    enum class State { Idle, Busy, Error, Manual };
    void setState(State state);

    State m_state = State::Idle;
    QString m_copyText;
    QLabel *m_status = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_skip = nullptr;
    QPushButton *m_later = nullptr;
    QPushButton *m_install = nullptr;
    QPushButton *m_cancel = nullptr;
    QPushButton *m_retry = nullptr;
    QPushButton *m_copy = nullptr;
};

#endif // UPDATEDIALOG_H
