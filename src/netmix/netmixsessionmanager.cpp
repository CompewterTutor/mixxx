#include "netmix/netmixsessionmanager.h"

#include "control/controlobject.h"
#include "control/controlproxy.h"
#include "moc_netmixsessionmanager.cpp"
#include "netmix/channelownership.h"
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

    m_pQuantizeCO = new ControlObject(ConfigKey("[Netmix]", "quantize"));
    m_pQuantizeCO->set(0.0);
}

NetmixSessionManager::~NetmixSessionManager() {
    deleteSubComponents();
    delete m_pStatusCO;
    delete m_pQuantizeCO;
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
    connect(m_pTcpSession, &TcpSession::helloComplete,
            this, &NetmixSessionManager::onHelloComplete);
    connect(m_pTcpSession, &TcpSession::messageReceived,
            this, &NetmixSessionManager::onTcpMessageReceived);

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
    connect(m_pTcpSession, &TcpSession::helloComplete,
            this, &NetmixSessionManager::onHelloComplete);
    connect(m_pTcpSession, &TcpSession::messageReceived,
            this, &NetmixSessionManager::onTcpMessageReceived);

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
    if (m_pChannelOwnership) {
        m_pChannelOwnership->autoReleaseAll();
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

    // Create quantizer
    m_pQuantizer = new NetmixQuantizer(this);
    m_pBpmProxy = new ControlProxy(
            ConfigKey(QStringLiteral("[InternalClock]"), QStringLiteral("bpm")),
            this);

    // Create channel ownership
    m_pChannelOwnership = new ChannelOwnership(
            m_pTcpSession->selfPeerId(), this);
    m_pCapture->setOwnership(m_pChannelOwnership);
    m_pApplier->setOwnership(m_pChannelOwnership);

    // Apply buffered pre-assignment from handshake
    applyPreAssignment();

    // Wire quantize CO -> quantizer (non-zero = enabled)
    connect(m_pQuantizeCO, &ControlObject::valueChanged,
            this, [this](double value) {
                if (m_pQuantizer) {
                    m_pQuantizer->setEnabled(value > 0.5);
                }
            });

    // Wire capture -> packer
    connect(m_pCapture, &ControlCapture::captured,
            m_pPacker, &InputFramePacker::addEvent);

    // Wire clock tick -> packer -> UDP send
    connect(&m_sessionClock, &SessionClock::tickAdvanced,
            this, &NetmixSessionManager::onTickAdvanced);

    // Wire UDP receive -> applier
    connect(m_pUdpChannel, &UdpChannel::inputFrameReceived,
            this, &NetmixSessionManager::onInputFrameReceived);

    // Wire ownership signals -> TCP sends
    if (m_pChannelOwnership) {
        connect(m_pChannelOwnership, &ChannelOwnership::claimRequested,
                this, [this](quint16 channelId) {
                    NetmixOwnershipClaim payload;
                    payload.channelId = channelId;
                    NetmixMessage msg{NetmixMessageType::OwnershipClaim, payload};
                    if (m_pTcpSession) m_pTcpSession->sendMessage(msg);
                });
        connect(m_pChannelOwnership, &ChannelOwnership::releaseRequested,
                this, [this](quint16 channelId) {
                    NetmixOwnershipRelease payload;
                    payload.channelId = channelId;
                    NetmixMessage msg{NetmixMessageType::OwnershipRelease, payload};
                    if (m_pTcpSession) m_pTcpSession->sendMessage(msg);
                });
    }

    setState(Connected);
}

void NetmixSessionManager::onTickAdvanced(quint32 tick) {
    if (!m_pPacker || !m_pUdpChannel) {
        return;
    }

    quint32 snappedTick = tick;
    if (m_pQuantizer && m_pBpmProxy) {
        double bpm = m_pBpmProxy->get();
        snappedTick = m_pQuantizer->snap(tick, bpm, SessionClock::kTickRate);
    }

    m_pPacker->finishTick(snappedTick);
    m_pUdpChannel->sendFrames(m_pPacker->framesForSend());
}

void NetmixSessionManager::onInputFrameReceived(quint32 baseTick,
        QVector<NetmixInputFrameEvent> events) {
    if (!m_pApplier) {
        return;
    }

    // Snap tick for consistent replay (value consumed when InputBuffer is wired)
    quint32 snappedTick = baseTick;
    if (m_pQuantizer && m_pBpmProxy) {
        double bpm = m_pBpmProxy->get();
        snappedTick = m_pQuantizer->snap(baseTick, bpm, SessionClock::kTickRate);
    }
    Q_UNUSED(snappedTick);
    m_pCapture->setMuted(true);
    m_pApplier->setOwnershipFilterEnabled(true);
    for (const auto& evt : events) {
        m_pApplier->apply(evt.wireId, evt.value);
    }
    m_pApplier->setOwnershipFilterEnabled(false);
    m_pCapture->setMuted(false);
}

void NetmixSessionManager::onHelloComplete(quint8 peerId,
        const QVector<quint16>& remotePreassigned) {
    Q_UNUSED(peerId);
    // Buffer until ChannelOwnership is created in onTcpConnected
    m_bufferedRemotePreassignment = remotePreassigned;
    if (m_pChannelOwnership) {
        applyPreAssignment();
    }
}

void NetmixSessionManager::onTcpMessageReceived(const NetmixMessage& msg) {
    if (!m_pChannelOwnership) {
        return;
    }

    switch (msg.type) {
    case NetmixMessageType::OwnershipClaim: {
        const auto* p = std::get_if<NetmixOwnershipClaim>(&msg.payload);
        if (!p) break;
        quint8 remotePeerId = m_pTcpSession ? m_pTcpSession->remotePeerId() : 1;
        auto action = m_pChannelOwnership->handleRemoteClaim(
                p->channelId, remotePeerId);
        if (action == ChannelOwnership::GrantAction) {
            NetmixOwnershipGrant grantPayload;
            grantPayload.channelId = p->channelId;
            NetmixMessage grantMsg{NetmixMessageType::OwnershipGrant, grantPayload};
            if (m_pTcpSession) m_pTcpSession->sendMessage(grantMsg);
        } else if (action == ChannelOwnership::DenyAction) {
            NetmixOwnershipDeny denyPayload;
            denyPayload.channelId = p->channelId;
            denyPayload.reason = 1; // Already owned
            NetmixMessage denyMsg{NetmixMessageType::OwnershipDeny, denyPayload};
            if (m_pTcpSession) m_pTcpSession->sendMessage(denyMsg);
        }
        break;
    }
    case NetmixMessageType::OwnershipGrant: {
        const auto* p = std::get_if<NetmixOwnershipGrant>(&msg.payload);
        if (!p) break;
        m_pChannelOwnership->handleGrant(p->channelId);
        break;
    }
    case NetmixMessageType::OwnershipDeny: {
        const auto* p = std::get_if<NetmixOwnershipDeny>(&msg.payload);
        if (!p) break;
        m_pChannelOwnership->handleDeny(p->channelId, p->reason);
        break;
    }
    case NetmixMessageType::OwnershipRelease: {
        const auto* p = std::get_if<NetmixOwnershipRelease>(&msg.payload);
        if (!p) break;
        m_pChannelOwnership->handleRelease(p->channelId);
        break;
    }
    default:
        break;
    }
}

void NetmixSessionManager::onTcpDisconnected() {
    if (m_pCapture) {
        m_pCapture->stop();
    }
    if (m_pChannelOwnership) {
        m_pChannelOwnership->autoReleaseAll();
    }
    deleteSubComponents();
    setState(Idle);
}

void NetmixSessionManager::applyPreAssignment() {
    if (!m_pChannelOwnership || !m_pTcpSession) {
        return;
    }
    m_pChannelOwnership->setLocalPreAssignment(
            m_pTcpSession->preassignedChannels());
    m_pChannelOwnership->setRemotePreAssignment(
            m_bufferedRemotePreassignment);
    m_pChannelOwnership->resolvePreAssignment();
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
    if (m_pQuantizer) {
        m_pQuantizer->disconnect(this);
    }
    if (m_pBpmProxy) {
        m_pBpmProxy->disconnect(this);
    }
    if (m_pQuantizeCO) {
        m_pQuantizeCO->disconnect(this);
    }
    if (m_pChannelOwnership) {
        m_pChannelOwnership->disconnect(this);
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
    if (m_pQuantizer) {
        m_pQuantizer->deleteLater();
        m_pQuantizer = nullptr;
    }
    if (m_pBpmProxy) {
        m_pBpmProxy->deleteLater();
        m_pBpmProxy = nullptr;
    }
    if (m_pCapture) {
        m_pCapture->deleteLater();
        m_pCapture = nullptr;
    }
    if (m_pUdpChannel) {
        m_pUdpChannel->deleteLater();
        m_pUdpChannel = nullptr;
    }
    if (m_pChannelOwnership) {
        m_pChannelOwnership->deleteLater();
        m_pChannelOwnership = nullptr;
    }
    if (m_pTcpSession) {
        m_pTcpSession->deleteLater();
        m_pTcpSession = nullptr;
    }
}
