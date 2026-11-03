#include <QSignalSpy>
#include <QTest>
#include "ui/widgets/frequencydisplaywidget.h"

// FrequencyDisplayWidget entry: FREQ ENT opens an empty entry, typed digits fill in from the right,
// Enter sends them as 11 digits, and cancelling shows the latest frequency the radio reported.
//
// Digit tuning (fork): before anything is typed, the arrow keys work on the current frequency
// instead. Left/Right pick a digit, Up/Down tune by its place value, so a laptop without a wheel can
// still step by 100 kHz or 1 MHz. Typing a digit switches to type-in.
class TestFrequencyDisplayWidget : public QObject {
    Q_OBJECT

private:
    // Show the widget and open entry the way FREQ ENT does. A click picks the tuning rate instead.
    static void enterEdit(FrequencyDisplayWidget &w) {
        w.setFrequency("7.031.415");
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.setFocus();
        w.beginEntry();
        QVERIFY(w.isEditing());
    }

    static qint64 lastDelta(const QSignalSpy &spy) { return spy.isEmpty() ? 0 : spy.last().at(0).value<qint64>(); }

    static void press(FrequencyDisplayWidget &w, Qt::Key key, int times = 1) {
        for (int i = 0; i < times; ++i)
            QTest::keyClick(&w, key);
    }

private slots:
    // ---- type-in (upstream) ----

    void testEnterWithoutTypingSendsNothing() {
        FrequencyDisplayWidget w;
        enterEdit(w);
        QSignalSpy spy(&w, &FrequencyDisplayWidget::frequencyEntered);
        QTest::keyClick(&w, Qt::Key_Return);
        QVERIFY(!w.isEditing());
        QCOMPARE(spy.count(), 0);
        QCOMPARE(w.frequency(), QString("00007031415"));
    }

    void testEnterAfterTypingSendsElevenDigits() {
        FrequencyDisplayWidget w;
        enterEdit(w);
        QSignalSpy spy(&w, &FrequencyDisplayWidget::frequencyEntered);
        for (const Qt::Key key : {Qt::Key_7, Qt::Key_0, Qt::Key_3, Qt::Key_1, Qt::Key_4, Qt::Key_1, Qt::Key_9})
            QTest::keyClick(&w, key);
        QTest::keyClick(&w, Qt::Key_Return);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("00007031419"));
    }

    void testCancelAfterRadioUpdateKeepsLatestFrequency() {
        FrequencyDisplayWidget w;
        enterEdit(w);
        w.setFrequency("7.032.415"); // the radio moved while the operator was typing
        QTest::keyClick(&w, Qt::Key_Escape);
        QVERIFY(!w.isEditing());
        QCOMPARE(w.frequency(), QString("00007032415"));
    }

    // ---- digit tuning ----

    // With no tuning rate shown, the first arrow press starts on the kHz digit.
    void testUpStartsOnTheKhzDigit() {
        FrequencyDisplayWidget w;
        enterEdit(w);
        QSignalSpy spy(&w, &FrequencyDisplayWidget::digitTuneRequested);
        press(w, Qt::Key_Up);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(lastDelta(spy), qint64(1000));
        QVERIFY(w.isEditing());
    }

    // With a tuning rate shown (a digit was clicked), digit tuning starts on that digit.
    void testStartsOnTheTuningRateDigit() {
        FrequencyDisplayWidget w;
        w.setTuningRateDigit(1); // 10 Hz
        enterEdit(w);
        QSignalSpy spy(&w, &FrequencyDisplayWidget::digitTuneRequested);
        press(w, Qt::Key_Down);
        QCOMPARE(lastDelta(spy), qint64(-10));
    }

    void testLeftAndRightMoveBetweenDigits() {
        FrequencyDisplayWidget w;
        enterEdit(w);
        QSignalSpy spy(&w, &FrequencyDisplayWidget::digitTuneRequested);
        press(w, Qt::Key_Left, 2); // kHz -> 100 kHz
        press(w, Qt::Key_Up);
        QCOMPARE(lastDelta(spy), qint64(100000));
        press(w, Qt::Key_Right, 4); // 100 kHz -> 10 Hz
        press(w, Qt::Key_Down);
        QCOMPARE(lastDelta(spy), qint64(-10));
        QCOMPARE(spy.count(), 2); // Left/Right only move; they tune nothing
    }

    // Left stops on the leftmost digit shown (1 MHz for 7 MHz); Right stops on 1 Hz.
    void testCursorStopsAtTheShownDigits() {
        FrequencyDisplayWidget w;
        enterEdit(w);
        QSignalSpy spy(&w, &FrequencyDisplayWidget::digitTuneRequested);
        press(w, Qt::Key_Left, 10);
        press(w, Qt::Key_Up);
        QCOMPARE(lastDelta(spy), qint64(1000000));
        press(w, Qt::Key_Right, 20);
        press(w, Qt::Key_Up);
        QCOMPARE(lastDelta(spy), qint64(1));
    }

    // Above 10 MHz more digits are shown, so the cursor can reach them (e.g. 1 GHz on 1.296 GHz).
    void testCursorReachesHigherDigitsOnHigherBands() {
        FrequencyDisplayWidget w;
        enterEdit(w);
        w.setFrequency("1.296.100.000");
        QSignalSpy spy(&w, &FrequencyDisplayWidget::digitTuneRequested);
        press(w, Qt::Key_Left, 20);
        press(w, Qt::Key_Up);
        QCOMPARE(lastDelta(spy), qint64(1000000000));
    }

    // While digit tuning, the display shows the radio's frequency as it replies.
    void testDisplayFollowsTheRadioWhileDigitTuning() {
        FrequencyDisplayWidget w;
        enterEdit(w);
        press(w, Qt::Key_Up);
        w.setFrequency("7.032.415"); // the radio's reply to the tune
        QCOMPARE(w.displayText(), QString("7.032.415"));
        QVERIFY(w.isEditing());
    }

    // Enter after digit tuning sends nothing (the radio already has it) and closes the entry.
    void testEnterAfterDigitTuningSendsNothing() {
        FrequencyDisplayWidget w;
        enterEdit(w);
        QSignalSpy entered(&w, &FrequencyDisplayWidget::frequencyEntered);
        press(w, Qt::Key_Up);
        w.setFrequency("7.032.415");
        press(w, Qt::Key_Return);
        QVERIFY(!w.isEditing());
        QCOMPARE(entered.count(), 0);
        QCOMPARE(w.frequency(), QString("00007032415"));
    }

    // Typing a digit switches to type-in: the arrows no longer tune, and Enter sends what was typed.
    void testTypingSwitchesToTypeIn() {
        FrequencyDisplayWidget w;
        enterEdit(w);
        QSignalSpy tuned(&w, &FrequencyDisplayWidget::digitTuneRequested);
        QSignalSpy entered(&w, &FrequencyDisplayWidget::frequencyEntered);
        press(w, Qt::Key_Up);
        press(w, Qt::Key_1);
        press(w, Qt::Key_4);
        press(w, Qt::Key_Up);
        QCOMPARE(tuned.count(), 1);
        QCOMPARE(w.displayText(), QString("14"));
        press(w, Qt::Key_Return);
        QCOMPARE(entered.count(), 1);
        QCOMPARE(entered.at(0).at(0).toString(), QString("00000000014"));
    }

    void testArrowsDoNothingOutsideEntry() {
        FrequencyDisplayWidget w;
        w.setFrequency("7.031.415");
        QSignalSpy spy(&w, &FrequencyDisplayWidget::digitTuneRequested);
        press(w, Qt::Key_Up);
        QCOMPARE(spy.count(), 0);
    }

    // A fresh entry starts again from the default digit, not where the last one left the cursor.
    void testEachEntryStartsAfresh() {
        FrequencyDisplayWidget w;
        enterEdit(w);
        QSignalSpy spy(&w, &FrequencyDisplayWidget::digitTuneRequested);
        press(w, Qt::Key_Left, 2);
        press(w, Qt::Key_Escape);
        w.beginEntry();
        press(w, Qt::Key_Up);
        QCOMPARE(lastDelta(spy), qint64(1000));
    }
};

QTEST_MAIN(TestFrequencyDisplayWidget)
#include "test_frequencydisplaywidget.moc"
