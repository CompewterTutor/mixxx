#include "netmix/netmixsessionmanager.h"

#include "control/controlobject.h"
#include "moc_netmixsessionmanager.cpp"
#include "util/logger.h"

namespace {
const mixxx::Logger kLogger("NetmixSessionManager");
} // namespace

NetmixSessionManager::NetmixSessionManager(QObject* parent)
        : QObject(parent),
          m_state(Idle) {
    m_pStatusCO = new ControlObject(ConfigKey("[Netmix]", "status"));
    m_pStatusCO->setReadOnly();
    m_pStatusCO->forceSet(static_cast<double>(Idle));
}

NetmixSessionManager::~NetmixSessionManager() {
    delete m_pStatusCO;
}

void NetmixSessionManager::setState(SessionState state) {
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit sessionStateChanged(state);
    m_pStatusCO->forceSet(static_cast<double>(state));
}

void NetmixSessionManager::hostSession() {
    kLogger.info() << "hostSession() called - not yet implemented (1.3.4)";
}

void NetmixSessionManager::joinSession() {
    kLogger.info() << "joinSession() called - not yet implemented (1.3.4)";
}

void NetmixSessionManager::leaveSession() {
    kLogger.info() << "leaveSession() called - not yet implemented (1.3.4)";
}
