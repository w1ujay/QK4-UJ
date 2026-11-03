#include <QSignalSpy>
#include <QTest>
#include "ui/widgets/frequencydisplaywidget.h"

// FrequencyDisplayWidget entry: FREQ ENT opens an empty entry, typed digits fill in from the right,
// Enter sends them as 11 digits, and cancelling shows the latest frequency the radio reported.
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

private slots:
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

    // The fork's Up/Down digit tuning needed the old cursor entry; nothing emits it in this design.
    void testUpDuringEntryDoesNotDigitTune() {
        FrequencyDisplayWidget w;
        enterEdit(w);
        QSignalSpy spy(&w, &FrequencyDisplayWidget::digitTuneRequested);
        QTest::keyClick(&w, Qt::Key_Up);
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TestFrequencyDisplayWidget)
#include "test_frequencydisplaywidget.moc"
