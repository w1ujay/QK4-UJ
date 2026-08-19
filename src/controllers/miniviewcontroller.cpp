#include "miniviewcontroller.h"

#include "controllers/dxclustercontroller.h"
#include "mainwindow.h"
#include "models/radiostate.h"
#include "settings/radiosettings.h"
#include "ui/miniviewwindow.h"
#include "ui/widgets/frequencydisplaywidget.h"
#include "ui/widgets/vfowidget.h"

#include <limits>

MiniViewController::MiniViewController(RadioState *radioState, DxClusterController *dxClusterController,
                                       VFOWidget *vfoA, VFOWidget *vfoB, MainWindow *mainWindow, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_dxClusterController(dxClusterController), m_vfoA(vfoA), m_vfoB(vfoB),
      m_mainWindow(mainWindow), m_window(new MiniViewWindow(nullptr)) {

    m_window->hide();

    // Double-click / restore control on the mini view returns to the full window.
    connect(m_window, &MiniViewWindow::restoreRequested, this, [this]() {
        m_window->hide();
        emit restoreRequested();
    });

    // BAND / MODE buttons restore the full window first, then ask MainWindow to
    // open the popup there — PopupManager positions popups against the bottom
    // menu bar, which isn't visible while the mini view is up.
    connect(m_window, &MiniViewWindow::bandClicked, this, [this]() {
        m_window->hide();
        emit restoreRequested();
        emit bandPopupRequested();
    });
    connect(m_window, &MiniViewWindow::modeClicked, this, [this]() {
        m_window->hide();
        emit restoreRequested();
        emit modePopupRequested();
    });

    // === Live state → mini view (all guarded on visibility) ===
    connect(m_radioState, &RadioState::frequencyChanged, this, [this](quint64) {
        if (m_window->isVisible())
            m_window->setFrequencyA(m_vfoA->frequencyDisplay()->frequency());
    });
    connect(m_radioState, &RadioState::frequencyBChanged, this, [this](quint64) {
        if (m_window->isVisible())
            m_window->setFrequencyB(m_vfoB->frequencyDisplay()->frequency());
    });
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode) {
        if (!m_window->isVisible())
            return;
        m_window->setModeA(m_radioState->modeStringFull());
        m_window->setPanMode(m_radioState->modeStringFull());
    });
    connect(m_radioState, &RadioState::modeBChanged, this, [this](RadioState::Mode) {
        if (m_window->isVisible())
            m_window->setModeB(m_radioState->modeStringFullB());
    });
    connect(m_radioState, &RadioState::filterBandwidthChanged, this, [this](int bw) {
        if (m_window->isVisible())
            m_window->setPanFilterBandwidth(bw);
    });
    connect(m_radioState, &RadioState::ifShiftChanged, this, [this](int shift) {
        if (m_window->isVisible())
            m_window->setPanIfShift(shift);
    });
    connect(m_radioState, &RadioState::cwPitchChanged, this, [this](int pitch) {
        if (m_window->isVisible())
            m_window->setPanCwPitch(pitch);
    });

    // === DX cluster spots → mini view spot strip ===
    if (m_dxClusterController) {
        connect(m_dxClusterController, &DxClusterController::spotsUpdated, this, &MiniViewController::refreshSpots);
    }

    // === Settings → section visibility ===
    connect(RadioSettings::instance(), &RadioSettings::miniViewSettingsChanged, this,
            [this]() { m_window->applySettings(); });
}

MiniViewController::~MiniViewController() {
    disconnect(this);
    delete m_window; // parentless so it survives MainWindow::hide()
}

void MiniViewController::showMiniView() {
    refreshAll();
    m_window->show();
    m_window->raise();
}

void MiniViewController::onSpectrumData(int receiver, const QByteArray &payload) {
    if (receiver != 0 || !m_window->isVisible() || !m_window->isPanadapterVisible())
        return;
    m_window->updateSpectrum(payload);
}

void MiniViewController::refreshAll() {
    m_window->setFrequencyA(m_vfoA->frequencyDisplay()->frequency());
    m_window->setFrequencyB(m_vfoB->frequencyDisplay()->frequency());
    m_window->setModeA(m_radioState->modeStringFull());
    m_window->setModeB(m_radioState->modeStringFullB());

    m_window->setPanMode(m_radioState->modeStringFull());
    m_window->setPanFilterBandwidth(m_radioState->filterBandwidth());
    m_window->setPanIfShift(m_radioState->ifShift());
    m_window->setPanCwPitch(m_radioState->cwPitch());
    m_window->setPanNotchFilter(m_radioState->manualNotchEnabled(), m_radioState->manualNotchPitch());

    refreshSpots();
}

void MiniViewController::refreshSpots() {
    if (!m_dxClusterController)
        return;

    // The mini view shows the whole aggregated cache; DxClusterController only
    // exposes a range query, so ask for the full representable span.
    const QVector<DxSpot> spots = m_dxClusterController->spotsForFrequencyRange(0, std::numeric_limits<qint64>::max());

    m_window->clearSpots();
    for (const DxSpot &spot : spots)
        m_window->addSpot(spot);
}
