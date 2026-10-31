#include <QtTest>

#include "update/updatepolicy.h"

Q_DECLARE_METATYPE(QSet<int>)

class TestUpdatePolicy : public QObject {
    Q_OBJECT

private slots:
    void shouldOffer_data();
    void shouldOffer();
    void detectsPlatforms();
    void assetKeys();
    void urlGuard_data();
    void urlGuard();
    void historyWindow();
    void historyComplete();
    void zipEntries_data();
    void zipEntries();

private:
    static UpdateManifest manifestWithHistory(int from, int to);
};

void TestUpdatePolicy::shouldOffer_data() {
    QTest::addColumn<int>("running");
    QTest::addColumn<int>("current"); // -1 means no current.txt
    QTest::addColumn<int>("offer");
    QTest::addColumn<int>("skipped");
    QTest::addColumn<QSet<int>>("failed");
    QTest::addColumn<bool>("expected");

    QTest::newRow("newer") << 13 << -1 << 14 << 0 << QSet<int>() << true;
    QTest::newRow("same as running") << 13 << -1 << 13 << 0 << QSet<int>() << false;
    QTest::newRow("current.txt already there") << 13 << 14 << 14 << 0 << QSet<int>() << false;
    QTest::newRow("current.txt behind offer") << 13 << 14 << 15 << 0 << QSet<int>() << true;
    QTest::newRow("skipped") << 13 << -1 << 14 << 14 << QSet<int>() << false;
    QTest::newRow("skipped lower") << 13 << -1 << 15 << 14 << QSet<int>() << true;
    QTest::newRow("failed") << 13 << -1 << 14 << 0 << QSet<int>{14} << false;
    QTest::newRow("other failed") << 13 << -1 << 15 << 0 << QSet<int>{14} << true;
}

void TestUpdatePolicy::shouldOffer() {
    QFETCH(int, running);
    QFETCH(int, current);
    QFETCH(int, offer);
    QFETCH(int, skipped);
    QFETCH(QSet<int>, failed);
    QFETCH(bool, expected);
    std::optional<int> cur;
    if (current >= 0)
        cur = current;
    QCOMPARE(::shouldOffer(running, cur, offer, skipped, failed), expected);
}

void TestUpdatePolicy::detectsPlatforms() {
    QCOMPARE(detectPlatform(true, false, false, "x86_64"), UpdatePlatform::Windows);
    QCOMPARE(detectPlatform(false, false, true, "arm64"), UpdatePlatform::Flatpak);
    QCOMPARE(detectPlatform(false, false, true, "x86_64"), UpdatePlatform::Flatpak);
    QCOMPARE(detectPlatform(false, false, false, "arm64"), UpdatePlatform::PiArm64);
    QCOMPARE(detectPlatform(false, false, false, "aarch64"), UpdatePlatform::PiArm64);
    QCOMPARE(detectPlatform(false, false, false, "x86_64"), UpdatePlatform::OtherLinux);
    QCOMPARE(detectPlatform(false, true, false, "arm64"), UpdatePlatform::Unsupported);
    QCOMPARE(detectPlatform(false, false, false, "riscv64"), UpdatePlatform::Unsupported);
}

void TestUpdatePolicy::assetKeys() {
    QCOMPARE(assetKeyFor(UpdatePlatform::Windows), QString("windows-x64"));
    QCOMPARE(assetKeyFor(UpdatePlatform::Flatpak), QString("linux-flatpak-x86_64"));
    QCOMPARE(assetKeyFor(UpdatePlatform::PiArm64), QString("raspberry-pi-arm64"));
    QVERIFY(assetKeyFor(UpdatePlatform::OtherLinux).isEmpty());
    QVERIFY(assetKeyFor(UpdatePlatform::Unsupported).isEmpty());
}

void TestUpdatePolicy::urlGuard_data() {
    QTest::addColumn<QString>("url");
    QTest::addColumn<bool>("expected");

    const QString base = "https://github.com/w1ujay/QK4-UJ/releases/download/";
    QTest::newRow("good") << base + "uj.13/x.zip" << true;
    QTest::newRow("http") << "http://github.com/w1ujay/QK4-UJ/releases/download/uj.13/x.zip" << false;
    QTest::newRow("other repo") << "https://github.com/someone/QK4/releases/download/uj.13/x.zip" << false;
    QTest::newRow("dotdot") << base + "../../evil" << false;
    QTest::newRow("encoded dotdot") << base + "%2e%2e/%2e%2e/evil" << false;
    QTest::newRow("other host") << "https://evil.example/w1ujay/QK4-UJ/releases/download/uj.13/x.zip" << false;
    QTest::newRow("userinfo") << "https://github.com@evil.example/w1ujay/QK4-UJ/releases/download/a" << false;
    QTest::newRow("bare prefix") << base << false;
}

void TestUpdatePolicy::urlGuard() {
    QFETCH(QString, url);
    QFETCH(bool, expected);
    QCOMPARE(isAllowedDownloadUrl(QUrl(url)), expected);
}

UpdateManifest TestUpdatePolicy::manifestWithHistory(int from, int to) {
    UpdateManifest m;
    for (int n = to; n >= from; --n) {
        UpdateRelease r;
        r.uj = n;
        r.version = QString("0.8.0-uj.%1").arg(n);
        m.history.append(r);
    }
    return m;
}

void TestUpdatePolicy::historyWindow() {
    bool missing = false;
    const auto list = releasesNewerThan(manifestWithHistory(3, 22), 2, &missing);
    QCOMPARE(list.size(), 20);
    QVERIFY(missing);
    QCOMPARE(list.first().uj, 22);
}

void TestUpdatePolicy::historyComplete() {
    bool missing = true;
    const auto list = releasesNewerThan(manifestWithHistory(3, 14), 12, &missing);
    QCOMPARE(list.size(), 2);
    QVERIFY(!missing);
    // A null out-pointer is allowed.
    QCOMPARE(releasesNewerThan(manifestWithHistory(3, 14), 12, nullptr).size(), 2);
}

void TestUpdatePolicy::zipEntries_data() {
    QTest::addColumn<QStringList>("entries");
    QTest::addColumn<bool>("expected");

    QStringList real = {"QK4/",
                        "QK4/QK4.exe",
                        "QK4/current.txt",
                        "QK4/versions/",
                        "QK4/versions/0.8.0-uj.13/",
                        "QK4/versions/0.8.0-uj.13/QK4.exe"};
    QTest::newRow("real listing") << real << true;
    QTest::newRow("with subfolder files")
        << (QStringList(real) << "QK4/versions/0.8.0-uj.13/platforms/qwindows.dll") << true;
    QTest::newRow("wrong version") << QStringList{"QK4/versions/0.8.0-uj.14/QK4.exe"} << false;
    QTest::newRow("traversal") << QStringList{"QK4/versions/0.8.0-uj.13/QK4.exe", "QK4/versions/0.8.0-uj.13/../../x"}
                               << false;
    QTest::newRow("absolute") << QStringList{"QK4/versions/0.8.0-uj.13/QK4.exe", "C:/x"} << false;
    QTest::newRow("no exe") << QStringList{"QK4/", "QK4/QK4.exe", "QK4/versions/0.8.0-uj.13/"} << false;
    QTest::newRow("stray") << QStringList{"QK4/versions/0.8.0-uj.13/QK4.exe", "QK4/stray.txt"} << false;
    QTest::newRow("backslash") << QStringList{"QK4/versions/0.8.0-uj.13/QK4.exe", "QK4\\evil.dll"} << false;
}

void TestUpdatePolicy::zipEntries() {
    QFETCH(QStringList, entries);
    QFETCH(bool, expected);
    QCOMPARE(validateZipEntries(entries, "0.8.0-uj.13"), expected);
}

QTEST_APPLESS_MAIN(TestUpdatePolicy)
#include "test_updatepolicy.moc"
