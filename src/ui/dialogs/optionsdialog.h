#ifndef OPTIONSDIALOG_H
#define OPTIONSDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QShowEvent>
#include <QHideEvent>
#include <QMediaDevices>

class RadioState;
class AudioController;
class HardwareController;
class CatServer;
class KPA1500Client;
class RFKitClient;
class DxClusterController;
class AboutPage;
class StationPage;
class AudioInputPage;
class AudioOutputPage;
class RigControlPage;
class CwKeyerPage;
class KpodPage;
class Kpa1500Page;
class RfkitPage;
class DxClusterPage;

/**
 * @brief Tabbed Options dialog. Left QListWidget drives a QStackedWidget holding the 8 option
 *        pages (About, Audio Input/Output, Rig Control, CW Keyer, KPOD, KPA1500, DX Cluster).
 *        Pages are lazy-constructed on first tab activation via @c ensurePageCreated.
 */
class OptionsDialog : public QDialog {
    Q_OBJECT

public:
    enum Page {
        PageAbout = 0,
        PageStation,
        PageAudioInput,
        PageAudioOutput,
        PageRigControl,
        PageCwKeyer,
        PageKpod,
        PageKpa1500,
        PageRfkit,
        PageDxCluster,
        PageCount
    };

    explicit OptionsDialog(RadioState *radioState, AudioController *audioController,
                           HardwareController *hardwareController, CatServer *catServer, KPA1500Client *kpa1500Client,
                           RFKitClient *rfkitClient, DxClusterController *dxClusterController,
                           QWidget *parent = nullptr);
    ~OptionsDialog();

    // Open the dialog with one page selected, for callers that know where the user needs to land
    // (e.g. the status bar's HaliKey indicator when no port has been chosen yet).
    void showPage(Page page);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void setupUi();
    void ensurePageCreated(int index);
    void refreshPage(int index);

    RadioState *m_radioState;
    AudioController *m_audioController;
    HardwareController *m_hardwareController;
    CatServer *m_catServer;
    KPA1500Client *m_kpa1500Client;
    RFKitClient *m_rfkitClient;
    DxClusterController *m_dxClusterController;
    QListWidget *m_tabList;
    QStackedWidget *m_pageStack;
    QMediaDevices *m_mediaDevices;
    bool m_pageCreated[PageCount] = {};

    // Page widgets
    AboutPage *m_aboutPage = nullptr;
    StationPage *m_stationPage = nullptr;
    AudioInputPage *m_audioInputPage = nullptr;
    AudioOutputPage *m_audioOutputPage = nullptr;
    RigControlPage *m_rigControlPage = nullptr;
    CwKeyerPage *m_cwKeyerPage = nullptr;
    KpodPage *m_kpodPage = nullptr;
    Kpa1500Page *m_kpa1500Page = nullptr;
    RfkitPage *m_rfkitPage = nullptr;
    DxClusterPage *m_dxClusterPage = nullptr;
};

#endif // OPTIONSDIALOG_H
