// Update popup (src/ui/dialogs/updatedialog.cpp), rendered offscreen.

#include "ui/dialogs/updatedialog.h"

#include <QApplication>
#include <QLabel>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

class UpdateDialogTests : public QObject {
    Q_OBJECT

private:
    static UpdateRelease release(int uj, const QString &version, const QStringList &added = {},
                                 const QStringList &fixed = {}) {
        UpdateRelease r;
        r.uj = uj;
        r.version = version;
        r.date = "2026-10-31";
        r.added = added;
        r.fixed = fixed;
        return r;
    }

    static QPushButton *button(const QWidget *w, const QString &text) {
        for (QPushButton *b : w->findChildren<QPushButton *>())
            if (b->text() == text)
                return b;
        return nullptr;
    }

    static bool shown(const QWidget *w, const QString &text) {
        QPushButton *b = button(w, text);
        return b && b->isVisible();
    }

    static QString allLabelText(const QWidget *w) {
        QString s;
        for (QLabel *l : w->findChildren<QLabel *>())
            s += l->text() + "\n";
        return s;
    }

    static QString changelog(const QWidget *w) {
        auto *e = w->findChild<QPlainTextEdit *>();
        return e ? e->toPlainText() : QString();
    }

private slots:
    void titleAndVersions() {
        UpdateDialog d("1.0", "1.1", {release(2, "1.1")}, false, true, nullptr);
        const QString t = allLabelText(&d);
        QVERIFY(t.contains("QK4 update available"));
        QVERIFY(t.contains("1.0"));
        QVERIFY(t.contains("1.1"));
    }

    void buttonLabelFollowsPlatform() {
        UpdateDialog a("1.0", "1.1", {release(2, "1.1")}, false, true, nullptr);
        a.show();
        QVERIFY(shown(&a, "Install now"));
        QVERIFY(!button(&a, "Download"));
        UpdateDialog b("1.0", "1.1", {release(2, "1.1")}, false, false, nullptr);
        b.show();
        QVERIFY(shown(&b, "Download"));
        QVERIFY(!button(&b, "Install now"));
    }

    void changelogIsPlainText() {
        UpdateDialog d("1.0", "1.1", {release(2, "1.1", {"<b>x</b>"})}, false, true, nullptr);
        auto *e = d.findChild<QPlainTextEdit *>();
        QVERIFY(e);
        QVERIFY(e->isReadOnly());
        QVERIFY(changelog(&d).contains("<b>x</b>"));
    }

    void groupsMultipleReleases() {
        UpdateDialog d("1.0", "1.2", {release(3, "1.2", {"newer"}), release(2, "1.1", {"older"})}, false, true,
                       nullptr);
        const QString c = changelog(&d);
        const int a = c.indexOf("1.2");
        const int b = c.indexOf("1.1");
        QVERIFY(a >= 0 && b >= 0);
        QVERIFY(a < b);
        QVERIFY(c.contains("newer") && c.contains("older"));
    }

    void emptyReleaseSaysNoChanges() {
        UpdateDialog d("1.0", "1.1", {release(2, "1.1")}, false, true, nullptr);
        QVERIFY(changelog(&d).contains("No listed changes"));
    }

    void olderMissingNote() {
        UpdateDialog a("1.0", "1.1", {release(2, "1.1")}, true, true, nullptr);
        QVERIFY(changelog(&a).contains("Older changes are not listed"));
        UpdateDialog b("1.0", "1.1", {release(2, "1.1")}, false, true, nullptr);
        QVERIFY(!changelog(&b).contains("Older changes are not listed"));
    }

    void progressSwapsButtons() {
        UpdateDialog d("1.0", "1.1", {release(2, "1.1")}, false, true, nullptr);
        d.show();
        d.showProgress(50, 100);
        QVERIFY(!shown(&d, "Install now"));
        QVERIFY(!shown(&d, "Later"));
        QVERIFY(!shown(&d, "Skip this version"));
        QVERIFY(shown(&d, "Cancel"));
        auto *bar = d.findChild<QProgressBar *>();
        QVERIFY(bar && bar->isVisible());
        QCOMPARE(bar->maximum(), 100);
        QCOMPARE(bar->value(), 50);
    }

    void indeterminateProgressIsBusyBar() {
        UpdateDialog d("1.0", "1.1", {release(2, "1.1")}, false, true, nullptr);
        d.show();
        d.showProgress(0, 0);
        auto *bar = d.findChild<QProgressBar *>();
        QVERIFY(bar);
        QCOMPARE(bar->minimum(), 0);
        QCOMPARE(bar->maximum(), 0);
        d.showProgress(10, 100);
        QCOMPARE(bar->maximum(), 100);
    }

    void cancelRestoresButtons() {
        UpdateDialog d("1.0", "1.1", {release(2, "1.1")}, false, true, nullptr);
        d.show();
        QSignalSpy spy(&d, &UpdateDialog::cancelRequested);
        d.showProgress(1, 2);
        QTest::mouseClick(button(&d, "Cancel"), Qt::LeftButton);
        QCOMPARE(spy.count(), 1);
        d.showIdleButtons();
        QVERIFY(shown(&d, "Install now"));
        QVERIFY(shown(&d, "Later"));
        QVERIFY(shown(&d, "Skip this version"));
        QVERIFY(!shown(&d, "Cancel"));
        auto *bar = d.findChild<QProgressBar *>();
        QVERIFY(!bar || !bar->isVisible());
    }

    void errorOffersRetryAndLater() {
        UpdateDialog d("1.0", "1.1", {release(2, "1.1")}, false, true, nullptr);
        d.show();
        QSignalSpy retry(&d, &UpdateDialog::retryRequested);
        QSignalSpy later(&d, &UpdateDialog::laterRequested);
        d.showProgress(1, 2);
        d.showError("Download failed");
        QVERIFY(allLabelText(&d).contains("Download failed"));
        QVERIFY(shown(&d, "Retry"));
        QVERIFY(shown(&d, "Later"));
        QVERIFY(!shown(&d, "Cancel"));
        QTest::mouseClick(button(&d, "Retry"), Qt::LeftButton);
        QCOMPARE(retry.count(), 1);
        QTest::mouseClick(button(&d, "Later"), Qt::LeftButton);
        QCOMPARE(later.count(), 1);
    }

    void manualStepsShowCopyOnlyWithText() {
        UpdateDialog d("1.0", "1.1", {release(2, "1.1")}, false, false, nullptr);
        d.show();
        d.showManualSteps("Run this", "cmd");
        QVERIFY(allLabelText(&d).contains("Run this"));
        QVERIFY(shown(&d, "Copy"));
        d.showManualSteps("Run this", "");
        QVERIFY(!shown(&d, "Copy"));
    }

    void buttonsEmitSignals() {
        UpdateDialog d("1.0", "1.1", {release(2, "1.1")}, false, true, nullptr);
        d.show();
        QSignalSpy install(&d, &UpdateDialog::installRequested);
        QSignalSpy skip(&d, &UpdateDialog::skipRequested);
        QTest::mouseClick(button(&d, "Install now"), Qt::LeftButton);
        QCOMPARE(install.count(), 1);
        QTest::mouseClick(button(&d, "Skip this version"), Qt::LeftButton);
        QCOMPARE(skip.count(), 1);
    }

    void escapeMeansLater() {
        UpdateDialog d("1.0", "1.1", {release(2, "1.1")}, false, true, nullptr);
        d.show();
        QSignalSpy later(&d, &UpdateDialog::laterRequested);
        QSignalSpy skip(&d, &UpdateDialog::skipRequested);
        QTest::keyClick(&d, Qt::Key_Escape);
        QCOMPARE(later.count(), 1);
        QCOMPARE(skip.count(), 0);
    }
};

QTEST_MAIN(UpdateDialogTests)
#include "test_updatedialog.moc"
