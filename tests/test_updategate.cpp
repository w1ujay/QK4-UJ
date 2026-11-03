// The startup gate (src/update/updategate.cpp): when the startup radio may connect, and when the popup may appear.

#include "update/updategate.h"

#include <QTest>

using Action = UpdateGate::Action;

class UpdateGateTests : public QObject {
    Q_OBJECT

private slots:
    void noUpdateAllowsRadio() {
        UpdateGate gate;
        QCOMPARE(gate.onCheckDone(false), Action::AllowStartupRadio);
        QVERIFY(!gate.startupRadioCancelled());
    }

    void updateShowsPopupThenRadio() {
        UpdateGate gate;
        QCOMPARE(gate.onCheckDone(true), Action::ShowPopup);
        QCOMPARE(gate.onPopupCleared(), Action::AllowStartupRadio);
        QVERIFY(!gate.startupRadioCancelled());
    }

    void manualConnectCancelsEverything() {
        UpdateGate gate;
        QCOMPARE(gate.onManualConnection(), Action::None);
        QVERIFY(gate.startupRadioCancelled());
        QCOMPARE(gate.onCheckDone(true), Action::Finish);
        QVERIFY(gate.startupRadioCancelled());
    }

    void manualConnectWithNoUpdate() {
        UpdateGate gate;
        gate.onManualConnection();
        QCOMPARE(gate.onCheckDone(false), Action::Finish);
    }

    // The gate is told when an attempt begins and never how it ended, so a failure cannot bring the radio back.
    void failedManualAttemptStillCancels() {
        UpdateGate gate;
        gate.onManualConnection();
        QCOMPARE(gate.onCheckDone(false), Action::Finish);
        QCOMPARE(gate.onPopupCleared(), Action::None);
        QVERIFY(gate.startupRadioCancelled());
    }

    void checkTimeoutActsLikeNoUpdate() {
        UpdateGate timedOut;
        UpdateGate answered;
        QCOMPARE(timedOut.onCheckDone(false), answered.onCheckDone(false));
    }

    void radioAllowedOnlyOnce() {
        UpdateGate gate;
        QCOMPARE(gate.onCheckDone(false), Action::AllowStartupRadio);
        QCOMPARE(gate.onCheckDone(false), Action::None);
        QCOMPARE(gate.onCheckDone(true), Action::None);
        QCOMPARE(gate.onPopupCleared(), Action::None);
    }

    void popupClearedTwiceAllowsRadioOnce() {
        UpdateGate gate;
        gate.onCheckDone(true);
        QCOMPARE(gate.onPopupCleared(), Action::AllowStartupRadio);
        QCOMPARE(gate.onPopupCleared(), Action::None);
    }

    // The startup connection itself passes through MainWindow::connectToRadio.
    void connectionAfterFinishChangesNothing() {
        UpdateGate gate;
        gate.onCheckDone(false);
        QCOMPARE(gate.onManualConnection(), Action::None);
        QVERIFY(!gate.startupRadioCancelled());
    }

    // The popup is modal, so this should not happen. If it ever does, the radio chosen by hand must stay.
    void manualConnectDuringPopupStopsRadio() {
        UpdateGate gate;
        QCOMPARE(gate.onCheckDone(true), Action::ShowPopup);
        QCOMPARE(gate.onManualConnection(), Action::None);
        QCOMPARE(gate.onPopupCleared(), Action::Finish);
        QVERIFY(gate.startupRadioCancelled());
    }

    void popupNeverShownBeforeCheckEnds() {
        UpdateGate gate;
        QCOMPARE(gate.onPopupCleared(), Action::None);
        QCOMPARE(gate.onCheckDone(false), Action::AllowStartupRadio);
    }
};

QTEST_APPLESS_MAIN(UpdateGateTests)
#include "test_updategate.moc"
