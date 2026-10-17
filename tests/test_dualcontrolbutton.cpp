#include <QSignalSpy>
#include <QTest>
#include "ui/widgets/dualcontrolbutton.h"

// DualControlButton keyboard behavior: Up/Down adjust the focused button's value, so control
// values (WPM, PWR, BW, SHIFT) are reachable on a laptop with no mouse.
class TestDualControlButton : public QObject {
    Q_OBJECT

private:
    static void showActive(DualControlButton &b) {
        b.setShowIndicator(true); // already the active button in its group
        b.show();
        QVERIFY(QTest::qWaitForWindowExposed(&b));
        b.setFocus();
    }

private slots:
    void testUpEmitsPositiveStep() {
        DualControlButton b;
        showActive(b);
        QSignalSpy spy(&b, &DualControlButton::valueScrolled);
        QTest::keyClick(&b, Qt::Key_Up);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 1);
    }

    void testDownEmitsNegativeStep() {
        DualControlButton b;
        showActive(b);
        QSignalSpy spy(&b, &DualControlButton::valueScrolled);
        QTest::keyClick(&b, Qt::Key_Down);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), -1);
    }

    // Matches the wheel: an inactive button activates first, then takes the step.
    void testKeyOnInactiveButtonActivatesFirst() {
        DualControlButton b;
        b.setShowIndicator(false);
        b.show();
        QVERIFY(QTest::qWaitForWindowExposed(&b));
        b.setFocus();
        QSignalSpy active(&b, &DualControlButton::becameActive);
        QSignalSpy scrolled(&b, &DualControlButton::valueScrolled);
        QTest::keyClick(&b, Qt::Key_Up);
        QCOMPARE(active.count(), 1);
        QCOMPARE(scrolled.count(), 1);
    }

    // Left/Right stay free for anything else; only Up/Down adjust the value.
    void testOtherKeysDoNotChangeValue() {
        DualControlButton b;
        showActive(b);
        QSignalSpy spy(&b, &DualControlButton::valueScrolled);
        QTest::keyClick(&b, Qt::Key_Left);
        QTest::keyClick(&b, Qt::Key_Right);
        QCOMPARE(spy.count(), 0);
    }

    void testAcceptsFocusFromClick() {
        DualControlButton b;
        QVERIFY(b.focusPolicy() != Qt::NoFocus);
    }
};

QTEST_MAIN(TestDualControlButton)
#include "test_dualcontrolbutton.moc"
