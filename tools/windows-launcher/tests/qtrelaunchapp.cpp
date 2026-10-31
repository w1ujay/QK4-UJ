// Proves that a restart made the way InterfaceScale::relaunch() makes it stays inside the launcher's job. The restart
// below must stay the same as src/settings/interfacescale.cpp: same QProcess calls, same startDetached().
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QThread>

namespace {

void recordStart(const QString &attempt, bool restarted) {
    const QString logDir = qEnvironmentVariable("FAKE_LOG_DIR");
    if (logDir.isEmpty())
        return;
    const QString base = logDir + QLatin1Char('/') + QString::number(QCoreApplication::applicationPid());
    // Windows FILETIME ticks, as fakeapp records them.
    const qint64 ticks = (QDateTime::currentMSecsSinceEpoch() + 11644473600000LL) * 10000;
    QFile out(base + QStringLiteral(".tmp"));
    if (!out.open(QIODevice::WriteOnly))
        return;
    out.write("exe=" + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toUtf8() + "\n");
    out.write("attempt=" + attempt.toUtf8() + "\n");
    out.write(QByteArray("restarted=") + (restarted ? "1" : "0") + "\n");
    out.write("started=" + QByteArray::number(ticks) + "\n");
    const QStringList args = QCoreApplication::arguments().mid(1);
    for (const QString &arg : args)
        out.write("arg=" + arg.toUtf8() + "\n");
    out.close();
    QFile::rename(base + QStringLiteral(".tmp"), base + QStringLiteral(".log"));
}

} // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    const QString attempt = qEnvironmentVariable("QK4_UPDATE_ATTEMPT");
    const bool restarted = qEnvironmentVariableIsSet("FAKE_RESTARTED");
    recordStart(attempt, restarted);

    if (!attempt.isEmpty() && !restarted) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("FAKE_RESTARTED"), QStringLiteral("1"));
        QProcess process;
        process.setProgram(QCoreApplication::applicationFilePath());
        process.setArguments(QCoreApplication::arguments().mid(1));
        process.setProcessEnvironment(env);
        return process.startDetached() ? 0 : 7;
    }

    if (restarted && qEnvironmentVariable("FAKE_MODE") == QStringLiteral("ready")) {
        QFile ready(QCoreApplication::applicationDirPath() + QStringLiteral("/.ready-") + attempt);
        if (ready.open(QIODevice::WriteOnly))
            ready.close();
    }
    QThread::sleep(300);
    return 0;
}
