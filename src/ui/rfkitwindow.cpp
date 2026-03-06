#include "rfkitwindow.h"
#include "rfkitpanel.h"
#include "k4styles.h"
#include "../settings/radiosettings.h"
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

namespace {
const int TitleBarHeight = 28;
const int BorderWidth = 2;
const int CloseButtonSize = 20;
} // namespace

RFKitWindow::RFKitWindow(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
    setAttribute(Qt::WA_TranslucentBackground);
    setupUi();
    restorePosition();
}

void RFKitWindow::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(BorderWidth, BorderWidth, BorderWidth, BorderWidth);
    mainLayout->setSpacing(0);

    // Title bar
    auto *titleBar = new QWidget(this);
    titleBar->setFixedHeight(TitleBarHeight);
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(8, 4, 4, 4);
    titleLayout->setSpacing(4);

    auto *titleLabel = new QLabel("RFKit", titleBar);
    titleLabel->setStyleSheet(
        QString("QLabel { color: %1; font-size: 11px; font-weight: bold; }").arg(K4Styles::Colors::AccentAmber));

    m_closeBtn = new QPushButton(titleBar);
    m_closeBtn->setFixedSize(CloseButtonSize, CloseButtonSize);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(QString("QPushButton { "
                                      "  background-color: transparent; "
                                      "  color: %1; "
                                      "  border: none; "
                                      "  font-size: %3px; "
                                      "  font-weight: bold; "
                                      "} "
                                      "QPushButton:hover { "
                                      "  background-color: %2; "
                                      "  border-radius: 3px; "
                                      "}")
                                  .arg(K4Styles::Colors::TextGray)
                                  .arg(K4Styles::Colors::BorderNormal)
                                  .arg(K4Styles::Dimensions::FontSizePopup));
    m_closeBtn->setText(QString::fromUtf8("\u00D7"));

    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        hide();
        emit closeRequested();
    });

    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    titleLayout->addWidget(m_closeBtn);

    m_panel = new RFKitPanel(this);

    mainLayout->addWidget(titleBar);
    mainLayout->addWidget(m_panel);

    setFixedSize(m_panel->width() + 2 * BorderWidth, m_panel->height() + TitleBarHeight + 2 * BorderWidth);
}

void RFKitWindow::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor bgColor(K4Styles::Colors::Background);
    QColor borderColor(K4Styles::Colors::BorderNormal);

    QPainterPath path;
    path.addRoundedRect(QRectF(1, 1, width() - 2, height() - 2), 6, 6);
    painter.fillPath(path, bgColor);

    painter.setPen(QPen(borderColor, 1));
    painter.drawPath(path);

    // Title bar separator line
    painter.setPen(QPen(borderColor, 1));
    painter.drawLine(BorderWidth, TitleBarHeight + BorderWidth, width() - BorderWidth, TitleBarHeight + BorderWidth);
}

QRect RFKitWindow::titleBarRect() const {
    return QRect(BorderWidth, BorderWidth, width() - 2 * BorderWidth, TitleBarHeight);
}

void RFKitWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && titleBarRect().contains(event->pos())) {
        m_dragging = true;
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
    QWidget::mousePressEvent(event);
}

void RFKitWindow::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragging) {
        move(event->globalPosition().toPoint() - m_dragPosition);
    } else {
        if (titleBarRect().contains(event->pos())) {
            setCursor(Qt::SizeAllCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
    }
    QWidget::mouseMoveEvent(event);
}

void RFKitWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (m_dragging) {
        m_dragging = false;
        savePosition();
    }
    QWidget::mouseReleaseEvent(event);
}

void RFKitWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    restorePosition();
}

void RFKitWindow::hideEvent(QHideEvent *event) {
    savePosition();
    QWidget::hideEvent(event);
}

void RFKitWindow::savePosition() {
    RadioSettings::instance()->setRfkitWindowPosition(pos());
}

void RFKitWindow::restorePosition() {
    QPoint savedPos = RadioSettings::instance()->rfkitWindowPosition();
    if (!savedPos.isNull()) {
        // Verify the saved position is visible on at least one screen
        QRect windowRect(savedPos, size());
        bool onScreen = false;
        for (QScreen *screen : QGuiApplication::screens()) {
            if (screen->availableGeometry().intersects(windowRect)) {
                onScreen = true;
                break;
            }
        }
        if (onScreen) {
            move(savedPos);
        } else {
            // Reset to center of primary screen
            if (QScreen *screen = QGuiApplication::primaryScreen()) {
                QRect screenGeo = screen->availableGeometry();
                move(screenGeo.center() - QPoint(width() / 2, height() / 2));
            }
            savePosition();
        }
    }
}
