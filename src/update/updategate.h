#ifndef UPDATEGATE_H
#define UPDATEGATE_H

// Decides when the startup radio may connect and whether the update popup may appear. It knows nothing about the
// network, the popup or the radio, so these rules can be tested alone.
//
// A manual connection attempt, whether or not it succeeds, ends the matter for this session. No popup appears, and the
// startup connection is cancelled, because ConnectionController::connectToRadio drops an existing connection and would
// replace the radio the person chose.
class UpdateGate {
public:
    enum class Action {
        None,              // nothing to do
        AllowStartupRadio, // the one time the startup connection may run
        ShowPopup,         // an update is on offer
        Finish             // the gate is done, and the startup connection was cancelled
    };

    // The check ended. updateFound is false for no update, a network failure, a bad file or a timeout.
    Action onCheckDone(bool updateFound);
    // A manual connection began. Takes effect when the check or the popup ends, and does nothing after that.
    Action onManualConnection();
    // Later, Skip or Cancel. Not called for a successful install, because QK4 is closing.
    Action onPopupCleared();

    bool startupRadioCancelled() const { return m_cancelled; }

private:
    enum class State { Checking, Popup, Finished };

    State m_state = State::Checking;
    bool m_cancelled = false;
};

#endif // UPDATEGATE_H
