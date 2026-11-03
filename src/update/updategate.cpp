#include "update/updategate.h"

UpdateGate::Action UpdateGate::onCheckDone(bool updateFound) {
    if (m_state != State::Checking)
        return Action::None;
    if (m_cancelled) {
        m_state = State::Finished;
        return Action::Finish;
    }
    if (updateFound) {
        m_state = State::Popup;
        return Action::ShowPopup;
    }
    m_state = State::Finished;
    return Action::AllowStartupRadio;
}

UpdateGate::Action UpdateGate::onManualConnection() {
    if (m_state != State::Finished)
        m_cancelled = true;
    return Action::None;
}

UpdateGate::Action UpdateGate::onPopupCleared() {
    if (m_state != State::Popup)
        return Action::None;
    m_state = State::Finished;
    return m_cancelled ? Action::Finish : Action::AllowStartupRadio;
}
