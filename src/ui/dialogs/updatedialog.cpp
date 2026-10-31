#include "ui/dialogs/updatedialog.h"
#include "ui/styling/k4styles.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
void appendSection(QStringList &out, const QString &heading, const QStringList &items) {
    if (items.isEmpty())
        return;
    out << heading;
    for (const QString &item : items)
        out << "  - " + item;
}
} // namespace

QString UpdateDialog::changelogText(const QList<UpdateRelease> &releases, bool olderChangesMissing) {
    QStringList blocks;
    for (const UpdateRelease &r : releases) {
        QStringList lines;
        lines << (r.date.isEmpty() ? r.version : QString("%1 (%2)").arg(r.version, r.date));
        QStringList body;
        appendSection(body, "Added", r.added);
        appendSection(body, "Fixed", r.fixed);
        appendSection(body, "Changed", r.changed);
        if (body.isEmpty())
            body << "No listed changes";
        lines << body;
        blocks << lines.join('\n');
    }
    QString text = blocks.join("\n\n");
    if (olderChangesMissing)
        text += (text.isEmpty() ? "" : "\n\n") + QString("Older changes are not listed.");
    return text;
}

UpdateDialog::UpdateDialog(QString currentVersion, QString newVersion, QList<UpdateRelease> releases,
                           bool olderChangesMissing, bool canInstall, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("QK4 update available");
    setModal(true);
    setMinimumSize(480, 380);
    setStyleSheet(QString("QDialog { background-color: %1; }").arg(K4Styles::Colors::Background));

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(K4Styles::Dimensions::PopupContentMargin);
    layout->setContentsMargins(K4Styles::Dimensions::PaddingLarge, K4Styles::Dimensions::PaddingLarge,
                               K4Styles::Dimensions::PaddingLarge, K4Styles::Dimensions::PaddingLarge);

    auto *title = new QLabel("QK4 update available", this);
    title->setTextFormat(Qt::PlainText);
    title->setStyleSheet(K4Styles::Dialog::titleLabel());
    layout->addWidget(title);

    auto *versions = new QLabel(QString("You have %1. Version %2 is available.").arg(currentVersion, newVersion), this);
    versions->setTextFormat(Qt::PlainText);
    versions->setStyleSheet(K4Styles::Dialog::formLabel());
    layout->addWidget(versions);

    // Plain text only: changelog entries come from a downloaded file and must never be markup.
    auto *changelog = new QPlainTextEdit(this);
    changelog->setReadOnly(true);
    changelog->setPlainText(changelogText(releases, olderChangesMissing));
    changelog->setStyleSheet(
        QString("QPlainTextEdit { background-color: %1; color: %2; border: 1px solid %3; "
                "border-radius: 4px; padding: %4px; font-size: %5px; }")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite, K4Styles::Colors::DialogBorder)
            .arg(K4Styles::Dimensions::PaddingSmall)
            .arg(K4Styles::Dimensions::FontSizeLarge));
    layout->addWidget(changelog, 1);

    m_status = new QLabel(this);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->hide();
    layout->addWidget(m_status);

    m_progress = new QProgressBar(this);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(K4Styles::Dimensions::PaddingLarge);
    m_progress->setStyleSheet(
        QString("QProgressBar { background-color: %1; border: 1px solid %2; border-radius: 4px; } "
                "QProgressBar::chunk { background-color: %3; border-radius: 3px; }")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::DialogBorder, K4Styles::Colors::AccentAmber));
    m_progress->hide();
    layout->addWidget(m_progress);

    m_skip = new QPushButton("Skip this version", this);
    m_later = new QPushButton("Later", this);
    m_install = new QPushButton(canInstall ? "Install now" : "Download", this);
    m_cancel = new QPushButton("Cancel", this);
    m_retry = new QPushButton("Retry", this);
    m_copy = new QPushButton("Copy", this);

    const QString buttonStyle = K4Styles::dialogButton();
    for (QPushButton *b : {m_skip, m_later, m_install, m_cancel, m_retry, m_copy}) {
        b->setStyleSheet(buttonStyle);
        b->setAutoDefault(false);
        b->setCursor(Qt::PointingHandCursor);
    }
    // The emphasised button: the selected popup look.
    m_install->setStyleSheet(K4Styles::popupButtonSelected());
    m_install->setAutoDefault(true);
    m_install->setDefault(true);

    auto *row = new QHBoxLayout();
    row->setSpacing(K4Styles::Dimensions::PaddingMedium);
    row->addWidget(m_skip);
    row->addStretch(1);
    row->addWidget(m_copy);
    row->addWidget(m_cancel);
    row->addWidget(m_retry);
    row->addWidget(m_later);
    row->addWidget(m_install);
    layout->addLayout(row);

    connect(m_install, &QPushButton::clicked, this, &UpdateDialog::installRequested);
    connect(m_skip, &QPushButton::clicked, this, [this] {
        emit skipRequested();
        QDialog::accept();
    });
    connect(m_later, &QPushButton::clicked, this, &UpdateDialog::reject);
    connect(m_cancel, &QPushButton::clicked, this, &UpdateDialog::cancelRequested);
    connect(m_retry, &QPushButton::clicked, this, &UpdateDialog::retryRequested);
    connect(m_copy, &QPushButton::clicked, this, [this] { QGuiApplication::clipboard()->setText(m_copyText); });

    setState(State::Idle);
}

void UpdateDialog::reject() {
    if (m_state == State::Busy) {
        emit cancelRequested();
        return;
    }
    emit laterRequested();
    QDialog::reject();
}

void UpdateDialog::setState(State state) {
    m_state = state;
    const bool idle = state == State::Idle;
    m_skip->setVisible(idle);
    m_install->setVisible(idle);
    m_later->setVisible(state != State::Busy);
    m_cancel->setVisible(state == State::Busy);
    m_retry->setVisible(state == State::Error);
    m_copy->setVisible(state == State::Manual && !m_copyText.isEmpty());
    m_progress->setVisible(state == State::Busy);
    m_status->setVisible(state == State::Error || state == State::Manual);
}

void UpdateDialog::showProgress(qint64 done, qint64 total) {
    if (total <= 0) {
        m_progress->setRange(0, 0); // busy bar
    } else {
        // QProgressBar is int-based; scale large byte counts down.
        const qint64 scale = total > 0x7fffffff ? (total >> 30) + 1 : 1;
        m_progress->setRange(0, int(total / scale));
        m_progress->setValue(int(qBound<qint64>(0, done, total) / scale));
    }
    setState(State::Busy);
}

void UpdateDialog::showError(const QString &message) {
    m_status->setStyleSheet(
        K4Styles::Dialog::labelText(K4Styles::Colors::ErrorRed, K4Styles::Dimensions::FontSizeLarge));
    m_status->setText(message);
    setState(State::Error);
}

void UpdateDialog::showManualSteps(const QString &text, const QString &copyText) {
    m_copyText = copyText;
    m_status->setStyleSheet(
        K4Styles::Dialog::labelText(K4Styles::Colors::TextWhite, K4Styles::Dimensions::FontSizeLarge));
    m_status->setText(text);
    setState(State::Manual);
}

void UpdateDialog::showIdleButtons() {
    setState(State::Idle);
}
