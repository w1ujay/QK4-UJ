#include <QtTest>

#include "../tools/windows-launcher/updatelayout.h"

Q_DECLARE_METATYPE(std::wstring)

class TestUpdateLayout : public QObject {
    Q_OBJECT

private:
    static std::wstring W(const char *s) { return QString::fromUtf8(s).toStdWString(); }
    static std::vector<VersionFolder> folders(std::initializer_list<VersionFolder> l) { return l; }

private slots:
    void ujFromName() {
        QCOMPARE(ujFromFolderName(L"0.8.0-uj.13"), std::optional<int>(13));
        QVERIFY(!ujFromFolderName(L"0.8.0-uj.test-1"));
        QVERIFY(!ujFromFolderName(L"0.8.0"));
        QVERIFY(!ujFromFolderName(L"0.8.0-uj."));
        QVERIFY(!ujFromFolderName(L"0.8.0-uj.99999999999"));
        QVERIFY(!ujFromFolderName(L"0.8.0-uj.13x"));
    }

    void currentPointer() {
        QCOMPARE(currentUj(L"0.8.0-uj.13\r\n"), std::optional<int>(13));
        QVERIFY(!currentUj(L""));
        QVERIFY(!currentUj(L"garbage"));
        QVERIFY(!currentUj(L"..\\..\\evil-uj.99"));
        QVERIFY(!currentUj(L"C:evil-uj.99"));
    }

    void selection() {
        auto f = folders({{L"0.8.0-uj.13", true, true}, {L"0.8.0-uj.14", true, false}, {L"0.8.0-uj.15", false, true}});
        QCOMPARE(selectVersion(L"0.8.0-uj.14\r\n", f), std::optional<std::wstring>(L"0.8.0-uj.14"));
        // current names a folder without exe -> fallback to highest healthy with exe
        QCOMPARE(selectVersion(L"0.8.0-uj.15", f), std::optional<std::wstring>(L"0.8.0-uj.13"));
        QCOMPARE(selectVersion(L"0.8.0-uj.99", f), std::optional<std::wstring>(L"0.8.0-uj.13"));
        QCOMPARE(selectVersion(L"", f), std::optional<std::wstring>(L"0.8.0-uj.13"));
        // none healthy -> highest with exe
        auto g = folders({{L"0.8.0-uj.13", true, false}, {L"0.8.0-uj.14", true, false}});
        QCOMPARE(selectVersion(L"", g), std::optional<std::wstring>(L"0.8.0-uj.14"));
        // nothing
        QVERIFY(!selectVersion(L"x", folders({{L"0.8.0-uj.13", false, true}})));
        QVERIFY(!selectVersion(L"", {}));
        // path escapes ignored
        auto h = folders({{L"..\\evil", true, true}, {L"0.8.0-uj.13", true, false}});
        QCOMPARE(selectVersion(L"..\\evil", h), std::optional<std::wstring>(L"0.8.0-uj.13"));
        QCOMPARE(selectVersion(L"C:evil", folders({{L"C:evil", true, true}})), std::optional<std::wstring>());
        QCOMPARE(selectVersion(L"a/b", folders({{L"a/b", true, true}})), std::optional<std::wstring>());
    }

    void quoting() {
        QCOMPARE(quoteArgument(L"K4 West"), std::wstring(L"\"K4 West\""));
        QCOMPARE(quoteArgument(L"a\\"), std::wstring(L"a\\"));
        QCOMPARE(quoteArgument(L"a b\\"), std::wstring(L"\"a b\\\\\""));
        QCOMPARE(quoteArgument(L"say \"hi\""), std::wstring(L"\"say \\\"hi\\\"\""));
        QCOMPARE(quoteArgument(L""), std::wstring(L"\"\""));
        QCOMPARE(quoteArgument(W("K4 S\xc3\xbc"
                                 "d")),
                 W("\"K4 S\xc3\xbc"
                   "d\""));
        QCOMPARE(quoteArgument(L"a\\\"b"), std::wstring(L"\"a\\\\\\\"b\""));
        QCOMPARE(joinCommandLine({L"--connect", L"K4 West", L"x"}), std::wstring(L"--connect \"K4 West\" x"));
    }

    void commit() {
        QVERIFY(shouldCommit(15, 14));
        QVERIFY(!shouldCommit(14, 15));
        QVERIFY(!shouldCommit(15, 15));
        QVERIFY(shouldCommit(15, std::nullopt));
    }

    void failure() {
        QCOMPARE(decideFailure(14, 14, false), FailureAction::StartCurrentNoNote);
        QCOMPARE(decideFailure(14, 15, true), FailureAction::StartCurrentNoNote);
        QCOMPARE(decideFailure(15, 14, true), FailureAction::KeepFolderWriteNote);
        QCOMPARE(decideFailure(15, 14, false), FailureAction::TrashFolderWriteNote);
        QCOMPARE(decideFailure(15, std::nullopt, false), FailureAction::TrashFolderWriteNote);
    }

    void cleanup() {
        std::vector<std::wstring> v;
        for (int i = 10; i <= 15; ++i)
            v.push_back(L"0.8.0-uj." + std::to_wstring(i));
        const std::wstring cur = L"0.8.0-uj.15";
        QVERIFY(foldersToClean(L"0.8.0-uj.14", cur, v, {}).empty());
        std::vector<std::wstring> want{L"0.8.0-uj.10", L"0.8.0-uj.11", L"0.8.0-uj.12", L"0.8.0-uj.13"};
        QCOMPARE(foldersToClean(cur, cur + L"\n", v, {}), want);

        v.push_back(L"0.8.0-uj.16");
        v.push_back(L"junk");
        v.push_back(L".trash-abc");
        QCOMPARE(foldersToClean(cur, cur, v, {}), want);
        want.push_back(L"0.8.0-uj.16");
        QCOMPARE(foldersToClean(cur, cur, v, {16}), want);
    }
};

QTEST_APPLESS_MAIN(TestUpdateLayout)
#include "test_updatelayout.moc"
