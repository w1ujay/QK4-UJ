#include "miniviewcontroller.h"

#include "controllers/connectioncontroller.h"
#include "controllers/dxclustercontroller.h"
#include "mainwindow.h"
#include "models/radiostate.h"
#include "settings/radiosettings.h"
#include "ui/miniviewwindow.h"
#include "ui/widgets/frequencydisplaywidget.h"
#include "ui/widgets/vfowidget.h"

#include <limits>

MiniViewController::MiniViewController(RadioState *radioState, ConnectionController *connection,
                                       DxClusterController *dxClusterController, VFOWidget *vfoA, VFOWidget *vfoB,
                                       MainWindow *mainWindow, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_dxClusterController(dxClusterController),
      m_vfoA(vfoA), m_vfoB(vfoB), m_mainWindow(mainWindow), m_window(new MiniViewWindow(nullptr)) {

    m_window->hide();

    // Double-click / restore control on the mini view returns to the full window.
    connect(m_window, &MiniViewWindow::restoreRequested, this, [this]() {
        hideWindow();
        emit restoreRequested();
    });

    // BAND / MODE buttons restore the full window first, then ask MainWindow to
    // open the popup there — PopupManager positions popups against the bottom
    // menu bar, which isn't visible while the mini view is up.
    connect(m_window, &MiniViewWindow::bandClicked, this, [this]() {
        hideWindow();
        emit restoreRequested();
        emit bandPopupRequested();
    });
    connect(m_window, &MiniViewWindow::modeClicked, this, [this]() {
        hideWindow();
        emit restoreRequested();
        emit modePopupRequested();
    });

    // === Mini view input → tuning (handled by MainWindow / SpectrumController) ===
    connect(m_window, &MiniViewWindow::tuneStepsRequested, this,
            [this](int steps) { emit tuneStepsRequested(m_radioState->bSetEnabled(), steps); });
    connect(m_window, &MiniViewWindow::frequencyScrolled, this, &MiniViewController::tuneStepsRequested);
    connect(m_window, &MiniViewWindow::panRightClicked, this, &MiniViewController::miniPanRightClicked);

    // Panadapter shown/hidden. The widget may have just been created, so push the
    // current pan settings first, then switch the K4 MiniPAN stream to match.
    connect(m_window, &MiniViewWindow::panadapterVisibilityChanged, this, [this](bool visible) {
        if (visible)
            refreshPan();
        updateMiniPanStream();
    });

    // A (re)connected K4 starts with MiniPAN off: forget any earlier #MP1 and re-evaluate.
    connect(m_connection, &ConnectionController::radioReady, this, [this]() {
        m_ownsMiniPanStream = false;
        updateMiniPanStream();
    });

    // === Live state → mini view (all guarded on visibility) ===
    // VfoFrequencyController is created before this controller, so the VFO displays
    // (with any RIT/XIT offset applied) are already updated when these run.
    connect(m_radioState, &RadioState::frequencyChanged, this, [this](quint64) {
        if (m_window->isVisible())
            m_window->setFrequencyA(m_vfoA->frequencyDisplay()->displayText());
    });
    connect(m_radioState, &RadioState::frequencyBChanged, this, [this](quint64) {
        if (m_window->isVisible())
            m_window->setFrequencyB(m_vfoB->frequencyDisplay()->displayText());
    });
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode mode) {
        if (!m_window->isVisible())
            return;
        m_window->setModeA(m_radioState->modeStringFull());
        m_window->setPanMode(RadioState::modeToString(mode));
    });
    connect(m_radioState, &RadioState::modeBChanged, this, [this](RadioState::Mode) {
        if (m_window->isVisible())
            m_window->setModeB(m_radioState->modeStringFullB());
    });
    connect(m_radioState, &RadioState::dataSubModeChanged, this, [this](int subMode) {
        if (m_window->isVisible())
            m_window->setPanDataSubMode(subMode);
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
    connect(m_radioState, &RadioState::notchChanged, this, [this]() {
        if (m_window->isVisible())
            m_window->setPanNotchFilter(m_radioState->manualNotchEnabled(), m_radioState->manualNotchPitch());
    });
    connect(m_radioState, &RadioState::averagingChanged, this, [this](int level) {
        if (m_window->isVisible())
            m_window->setPanAveraging(level);
    });
    connect(m_radioState, &RadioState::waterfallHeightChanged, this, [this](int percent) {
        if (m_window->isVisible())
            m_window->setPanWaterfallHeight(percent);
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
    updateMiniPanStream();
}

void MiniViewController::onSpectrumData(int receiver, const QByteArray &bins) {
    if (receiver != 0 || !m_window->isVisible() || !m_window->isPanadapterVisible())
        return;
    m_window->updateSpectrum(bins);
}

void MiniViewController::refreshFrequencies() {
    m_window->setFrequencyA(m_vfoA->frequencyDisplay()->displayText());
    m_window->setFrequencyB(m_vfoB->frequencyDisplay()->displayText());
}

void MiniViewController::refreshAll() {
    refreshFrequencies();
    m_window->setModeA(m_radioState->modeStringFull());
    m_window->setModeB(m_radioState->modeStringFullB());
    refreshPan();
    refreshSpots();
}

void MiniViewController::refreshPan() {
    // WHY modeToString + data sub-mode: the mini-pan's span/passband branches expect the base
    // mode name ("DATA") plus setDataSubMode, the same way VFOWidget's mini-pan is fed.
    m_window->setPanMode(RadioState::modeToString(m_radioState->mode()));
    m_window->setPanDataSubMode(m_radioState->dataSubMode());
    m_window->setPanFilterBandwidth(m_radioState->filterBandwidth());
    m_window->setPanIfShift(m_radioState->ifShift());
    m_window->setPanCwPitch(m_radioState->cwPitch());
    m_window->setPanNotchFilter(m_radioState->manualNotchEnabled(), m_radioState->manualNotchPitch());
    if (m_radioState->averaging() >= 1)
        m_window->setPanAveraging(m_radioState->averaging());
    if (m_radioState->waterfallHeight() >= 0)
        m_window->setPanWaterfallHeight(m_radioState->waterfallHeight());
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

void MiniViewController::hideWindow() {
    m_window->hide();
    updateMiniPanStream();
}

void MiniViewController::updateMiniPanStream() {
    // WHY: VFO A's mini-pan in the main window owns #MP via RadioState::miniPanAEnabled.
    // Only turn the stream on if nothing else has, and only undo what this controller did.
    const bool wanted = m_window->isVisible() && m_window->isPanadapterVisible();
    // Only claim the stream while connected — sendCAT drops commands otherwise; radioReady re-runs this.
    if (wanted && !m_ownsMiniPanStream && !m_radioState->miniPanAEnabled() && m_connection->isConnected()) {
        m_connection->sendCAT("#MP1;");
        m_ownsMiniPanStream = true;
    } else if (!wanted && m_ownsMiniPanStream) {
        if (!m_radioState->miniPanAEnabled())
            m_connection->sendCAT("#MP0;");
        m_ownsMiniPanStream = false;
    }
}
