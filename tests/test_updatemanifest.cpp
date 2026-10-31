#include <QtTest>

#include "update/updatemanifest.h"
#include "update/updateversion.h"

class TestUpdateManifest : public QObject {
    Q_OBJECT

private slots:
    void parseUjNumber_data();
    void parseUjNumber();
    void parsesValidManifest();
    void rejectsHtmlBody();
    void rejectsUnknownSchema();
    void rejectsMissingAssetFields();
    void rejectsNonIntegerSize();
    void acceptsEmptyChangeLists();

private:
    static QByteArray manifest(const QString &schema = "1", const QString &asset = QString(),
                               const QString &lists = QString());
};

void TestUpdateManifest::parseUjNumber_data() {
    QTest::addColumn<QString>("version");
    QTest::addColumn<int>("expected"); // -1 means no value

    QTest::newRow("release") << "0.8.0-uj.13" << 13;
    QTest::newRow("main build") << "main-3d4e37b" << -1;
    QTest::newRow("test tag") << "0.8.0-uj.test-1" << -1;
    QTest::newRow("empty number") << "0.8.0-uj." << -1;
    QTest::newRow("overflow") << "0.8.0-uj.99999999999" << -1;
    QTest::newRow("dev") << "dev" << -1;
    QTest::newRow("empty") << "" << -1;
}

void TestUpdateManifest::parseUjNumber() {
    QFETCH(QString, version);
    QFETCH(int, expected);
    const std::optional<int> n = UpdateVersion::parseUjNumber(version);
    if (expected < 0) {
        QVERIFY(!n.has_value());
    } else {
        QVERIFY(n.has_value());
        QCOMPARE(*n, expected);
    }
}

QByteArray TestUpdateManifest::manifest(const QString &schema, const QString &asset, const QString &lists) {
    const QString a = asset.isEmpty() ? QStringLiteral(R"({"name":"QK4.zip","url":"https://example.com/QK4.zip",)"
                                                       R"("size":44841368,"sha256":"abc123"})")
                                      : asset;
    const QString l = lists.isEmpty() ? QStringLiteral(R"("added":["a"],"fixed":["f"],"changed":["c"])") : lists;
    return QStringLiteral(R"({"schema":%1,"version":"0.8.0-uj.13","uj":13,"date":"2026-11-02",)"
                          R"("assets":{"windows-x64":%2},)"
                          R"("history":[{"uj":13,"version":"0.8.0-uj.13","date":"2026-11-02",%3}]})")
        .arg(schema, a, l)
        .toUtf8();
}

void TestUpdateManifest::parsesValidManifest() {
    const auto m = parseUpdateManifest(manifest());
    QVERIFY(m.has_value());
    QCOMPARE(m->uj, 13);
    QCOMPARE(m->version, QStringLiteral("0.8.0-uj.13"));
    QVERIFY(m->assets.contains("windows-x64"));
    QCOMPARE(m->assets["windows-x64"].size, qint64(44841368));
    QCOMPARE(m->assets["windows-x64"].url, QUrl("https://example.com/QK4.zip"));
    QCOMPARE(m->assets["windows-x64"].sha256Hex, QByteArray("abc123"));
    QCOMPARE(m->history.size(), 1);
    QCOMPARE(m->history.first().uj, 13);
    QCOMPARE(m->history.first().added, QStringList{"a"});
    QCOMPARE(m->history.first().fixed, QStringList{"f"});
    QCOMPARE(m->history.first().changed, QStringList{"c"});
}

void TestUpdateManifest::rejectsHtmlBody() {
    QVERIFY(!parseUpdateManifest("<html>login</html>").has_value());
}

void TestUpdateManifest::rejectsUnknownSchema() {
    QVERIFY(!parseUpdateManifest(manifest("2")).has_value());
}

void TestUpdateManifest::rejectsMissingAssetFields() {
    for (const char *drop : {"name", "url", "size", "sha256"}) {
        QJsonObject o =
            QJsonDocument::fromJson(R"({"name":"n","url":"https://e.com/x","size":1,"sha256":"ab"})").object();
        o.remove(drop);
        const QString asset = QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
        QVERIFY2(!parseUpdateManifest(manifest("1", asset)).has_value(), drop);
    }
}

void TestUpdateManifest::rejectsNonIntegerSize() {
    for (const char *size : {"1.5", "\"123\"", "-4", "null"}) {
        const QString asset = QStringLiteral("{\"name\":\"n\",\"url\":\"https://e.com/x\",\"size\":") + size +
                              QStringLiteral(",\"sha256\":\"ab\"}");
        QVERIFY2(!parseUpdateManifest(manifest("1", asset)).has_value(), size);
    }
}

void TestUpdateManifest::acceptsEmptyChangeLists() {
    const auto m = parseUpdateManifest(manifest("1", QString(), R"("added":[],"fixed":[],"changed":[])"));
    QVERIFY(m.has_value());
    QCOMPARE(m->history.size(), 1);
    QVERIFY(m->history.first().added.isEmpty());
    QVERIFY(m->history.first().fixed.isEmpty());
    QVERIFY(m->history.first().changed.isEmpty());
}

QTEST_APPLESS_MAIN(TestUpdateManifest)
#include "test_updatemanifest.moc"
