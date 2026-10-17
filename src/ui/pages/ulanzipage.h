#ifndef ULANZIPAGE_H
#define ULANZIPAGE_H

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QWidget>

class UlanziServer;

/**
 * @brief OptionsDialog "Ulanzi Dial" tab. Enables the localhost server the QK4 Ulanzi Studio plugin
 *        connects to, sets its port, and shows whether the plugin is connected.
 */
class UlanziPage : public QWidget {
    Q_OBJECT

public:
    explicit UlanziPage(UlanziServer *server, QWidget *parent = nullptr);

    void refresh();

private:
    void updateStatus();

    UlanziServer *m_server;
    QCheckBox *m_enableCheckbox = nullptr;
    QLineEdit *m_portEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
};

#endif // ULANZIPAGE_H
