#include "netmix/netmixsessionmanager.h"

#include "control/controlobject.h"
#include "moc_netmixsessionmanager.cpp"
#include "util/logger.h"

namespace {
const mixxx::Logger kLogger("NetmixSessionManager");
} // namespace

NetmixSessionManager::NetmixSessionManager(QObject* parent)
        : QObject(parent),
          m_state(Idle),
          m_enabled(false) {
    m_pStatusCO = new ControlObject(ConfigKey("[Netmix]", "status"));
    m_pStatusCO->setReadOnly();
    m_pStatusCO->forceSet(static_cast<double>(Idle));
}

NetmixSessionManager::~NetmixSessionManager() {
    deleteSubComponents();
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

void NetmixSessionManager::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled && m_state != Idle) {
        leaveSession();
    }
}

void NetmixSessionManager::hostSession(quint16 port) {
    if (m_state != Idle) {
        kLogger.warning() << "hostSession called but state is not Idle (current:"
                          << m_state << ")";
        return;
    }
    if (!m_enabled) {
        kLogger.warning() << "hostSession called but netmix is disabled";
        return;
    }

    m_pTcpSession = new TcpSession(this);
    m_pTcpSession->setDisplayName(QStringLiteral("netmix-host"));
    m_pTcpSession->setListenPort(port);
    if (!m_pTcpSession->listen()) {
        kLogger.warning() << "hostSession: listen failed on port" << port;
        deleteSubComponents();
        return;
    }

    m_pUdpChannel = new UdpChannel(this);
    quint16 actualTcpPort = m_pTcpSession->server()->serverPort();
    if (!m_pUdpChannel->bind(actualTcpPort)) {
        kLogger.warning() << "hostSession: UDP bind failed on port" << actualTcpPort;
        deleteSubComponents();
        return;
    }

    m_pTcpSession->setLocalUdpPort(m_pUdpChannel->socket()->localPort());

    connect(m_pTcpSession, &TcpSession::stateChanged,
            this, &NetmixSessionManager::onTcpStateChanged);

    setState(Connecting);
}

void NetmixSessionManager::joinSession(const QHostAddress& address, quint16 port) {
    if (m_state != Idle) {
        kLogger.warning() << "joinSession called but state is not Idle (current:"
                          << m_state << ")";
        return;
    }
    if (!m_enabled) {
        kLogger.warning() << "joinSession called but netmix is disabled";
        return;
    }

    m_pTcpSession = new TcpSession(this);
    m_pTcpSession->setDisplayName(QStringLiteral("netmix-client"));
    m_pTcpSession->setPeerAddress(address, port);

    m_pUdpChannel = new UdpChannel(this);
    if (!m_pUdpChannel->bind(0)) {
        kLogger.warning() << "joinSession: UDP bind failed on auto port";
        deleteSubComponents();
        return;
    }

    m_pTcpSession->setLocalUdpPort(m_pUdpChannel->socket()->localPort());

    // Client assumes host's UDP port == TCP port
    m_pUdpChannel->setPeer(address, port);

    connect(m_pTcpSession, &TcpSession::stateChanged,
            this, &NetmixSessionManager::onTcpStateChanged);

    m_pTcpSession->connectToPeer();
    setState(Connecting);
}

void NetmixSessionManager::leaveSession() {
    if (m_state == Idle) {
        return;
    }

    if (m_pCapture) {
        m_pCapture->stop();
    }
    if (m_pTcpSession) {
        m_pTcpSession->disconnectFromPeer();
    }

    deleteSubComponents();
    setState(Idle);
}

void NetmixSessionManager::onTcpStateChanged(TcpSession::State ts) {
    switch (ts) {
    case TcpSession::Listening:
    case TcpSession::Connecting:
    case TcpSession::Handshaking:
        break;
    case TcpSession::Connected:
        onTcpConnected();
        break;
    case TcpSession::Degraded:
        setState(Degraded);
        break;
    case TcpSession::Disconnected:
        onTcpDisconnected();
        break;
    }
}

void NetmixSessionManager::onTcpConnected() {
    // Host: after receiving Hello and knowing remote's UDP port, set up UDP peer
    if (m_pTcpSession->selfPeerId() == 0) {
        quint16 remoteUdpPort = m_pTcpSession->remoteUdpPort();
        if (remoteUdpPort == 0) {
            // Fallback: assume host's TCP port is the same
            remoteUdpPort = m_pTcpSession->server()->serverPort();
        }
        m_pUdpChannel->setPeer(m_pTcpSession->peerAddress(), remoteUdpPort);
    }

    // Create capture
    m_pCapture = new ControlCapture(this);
    m_pCapture->start(&m_sessionClock);

    // Create packer
    m_pPacker = new InputFramePacker(this);

    // Create applier
    m_pApplier = new ControlApplier(this);
    m_pApplier->setProxies(m_pCapture->proxies());

    // Wire capture -> packer
    connect(m_pCapture, &ControlCapture::captured,
            m_pPacker, &InputFramePacker::addEvent);

    // Wire clock tick -> packer -> UDP send
    connect(&m_sessionClock, &SessionClock::tickAdvanced,
            this, &NetmixSessionManager::onTickAdvanced);

    // Wire UDP receive -> applier
    connect(m_pUdpChannel, &UdpChannel::inputFrameReceived,
            this, &NetmixSessionManager::onInputFrameReceived);

    setState(Connected);
}

void NetmixSessionManager::onTickAdvanced(quint32 tick) {
    if (!m_pPacker || !m_pUdpChannel) {
        return;
    }
    m_pPacker->finishTick(tick);
    m_pUdpChannel->sendFrames(m_pPacker->framesForSend());
}

void NetmixSessionManager::onInputFrameReceived(quint32 baseTick,
        QVector<NetmixInputFrameEvent> events) {
    Q_UNUSED(baseTick);
    if (!m_pApplier) {
        return;
    }
    m_pCapture->setMuted(true);
    for (const auto& evt : events) {
        m_pApplier->apply(evt.wireId, evt.value);
    }
    m_pCapture->setMuted(false);
}

void NetmixSessionManager::onTcpDisconnected() {
    if (m_pCapture) {
        m_pCapture->stop();
    }
    deleteSubComponents();
    setState(Idle);
}

void NetmixSessionManager::deleteSubComponents() {
    // Disconnect signals first so we don't get callbacks during deferred deletion
    if (m_pTcpSession) {
        m_pTcpSession->disconnect(this);
    }
    if (m_pUdpChannel) {
        m_pUdpChannel->disconnect(this);
    }
    if (m_pCapture) {
        m_pCapture->disconnect(this);
    }
    if (m_pApplier) {
        m_pApplier->disconnect(this);
    }
    if (m_pPacker) {
        m_pPacker->disconnect(this);
    }

    // Use deleteLater to avoid deleting an object while it's emitting a signal
    if (m_pApplier) {
        m_pApplier->deleteLater();
        m_pApplier = nullptr;
    }
    if (m_pPacker) {
        m_pPacker->deleteLater();
        m_pPacker = nullptr;
    }
    if (m_pCapture) {
        m_pCapture->deleteLater();
        m_pCapture = nullptr;
    }
    if (m_pUdpChannel) {
        m_pUdpChannel->deleteLater();
        m_pUdpChannel = nullptr;
    }
    if (m_pTcpSession) {
        m_pTcpSession->deleteLater();
        m_pTcpSession = nullptr;
    }
}
