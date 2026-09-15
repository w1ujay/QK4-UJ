#include <QSignalSpy>
#include <QTest>
#include "ui/widgets/frequencydisplaywidget.h"

// FrequencyDisplayWidget edit-mode keyboard behavior: Up/Down on the cursor digit
// tunes by that digit's place value, and cancelling keeps the latest radio frequency.
class TestFrequencyDisplayWidget : public QObject {
    Q_OBJECT

private:
    // Show the widget and click it to enter edit mode (cursor starts at digit 0).
    static void enterEdit(FrequencyDisplayWidget &w) {
        w.setFrequency("7.031.415");
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, QPoint(2, w.height() / 2));
        QVERIFY(w.isEditing());
    }

    static qint64 onlyDelta(const QSignalSpy &spy) {
        if (spy.count() != 1)
            return -999999999;
        return spy.at(0).at(0).value<qint64>();
    }

private slots:
    void testUpOnOnesDigitTunesOneHz() {
        FrequencyDisplayWidget w;
        enterEdit(w);
        QSignalSpy spy(&w, &FrequencyDisplayWidget::digitTuneRequested);
        QTest::keyClick(&w, Qt::Key_End);
        QTest::keyClick(&w, Qt::Key_Up);
        QCOMPARE(onlyDelta(spy), qint64(1));
    }

    void testDownOnKhzDigitTunesMinusOneKhz() {
        FrequencyDisplayWidget w;
        enterEdit(w);
        QSignalSpy spy(&w, &FrequencyDisplayWidget::digitTuneRequested);
        QTest::keyClick(&w, Qt::Key_End);
        QTest::keyClick(&w, Qt::Key_Left);
        QTest::keyClick(&w, Qt::Key_Left);
        QTest::keyClick(&w, Qt::Key_Left);
        QTest::keyClick(&w, Qt::Key_Down);
        QCOMPARE(onlyDelta(spy), qint64(-1000));
    }

    void testUpOnLeftmostDigitTunesOneGhz() {
        FrequencyDisplayWidget w;
        enterEdit(w);
        QSignalSpy spy(&w, &FrequencyDisplayWidget::digitTuneRequested);
        QTest::keyClick(&w, Qt::Key_Home);
        QTest::keyClick(&w, Qt::Key_Up);
        QCOMPARE(onlyDelta(spy), qint64(1000000000));
    }

    void testDigitTuneStaysInEditMode() {
        FrequencyDisplayWidget w;
        enterEdit(w);
        QTest::keyClick(&w, Qt::Key_End);
        QTest::keyClick(&w, Qt::Key_Up);
        QVERIFY(w.isEditing());
    }

    void testUpAfterTypingDigitsDoesNotDigitTune() {
        FrequencyDisplayWidget w;
        enterEdit(w);
        QSignalSpy spy(&w, &FrequencyDisplayWidget::digitTuneRequested);
        QTest::keyClick(&w, Qt::Key_End);
        QTest::keyClick(&w, Qt::Key_9); // unsent typed digit (ones place was 5)
        QTest::keyClick(&w, Qt::Key_Up);
        QCOMPARE(spy.count(), 0);
        QCOMPARE(w.frequency(), QString("0007031419"));
    }

    void testUpWhenNotEditingDoesNotDigitTune() {
        FrequencyDisplayWidget w;
        w.setFrequency("7.031.415");
        QSignalSpy spy(&w, &FrequencyDisplayWidget::digitTuneRequested);
        QTest::keyClick(&w, Qt::Key_Up);
        QCOMPARE(spy.count(), 0);
    }

    void testCancelAfterRadioUpdateKeepsLatestFrequency() {
        FrequencyDisplayWidget w;
        enterEdit(w);
        w.setFrequency("7.032.415"); // radio echo after a digit tune
        QTest::keyClick(&w, Qt::Key_Escape);
        QVERIFY(!w.isEditing());
        QCOMPARE(w.frequency(), QString("0007032415"));
    }
};

QTEST_MAIN(TestFrequencyDisplayWidget)
#include "test_frequencydisplaywidget.moc"
