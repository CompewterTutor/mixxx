#include "netmix/tcpsession.h"

#include <QDataStream>
#include <QElapsedTimer>
#include <QIODevice>

#include "util/logger.h"

namespace {

constexpr int kHeartbeatIntervalMs = 1000;
constexpr int kDeadPeerCheckIntervalMs = 1000;

mixxx::Logger kLog("Netmix TcpSession");

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

TcpSession::TcpSession(QObject* parent)
        : QObject(parent) {
    m_lastReceiveTime.start();
    m_lastSendTime.start();
}

TcpSession::~TcpSession() {
    cleanupServer();
    cleanupSocket();
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void TcpSession::setListenPort(quint16 port) {
    m_listenPort = port;
}

void TcpSession::setPeerAddress(const QHostAddress& address, quint16 port) {
    m_peerAddress = address;
    m_peerPort = port;
}

void TcpSession::setDisplayName(const QString& name) {
    m_displayName = name;
}

void TcpSession::setSessionParams(quint16 tickRate, quint16 rollbackWindow) {
    m_tickRate = tickRate;
    m_rollbackWindow = rollbackWindow;
}

void TcpSession::setTimeoutsForTest(int degradedSec, int disconnectSec) {
    m_degradedTimeoutMs = degradedSec * 1000;
    m_disconnectTimeoutMs = disconnectSec * 1000;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool TcpSession::listen() {
    if (m_state != Disconnected) {
        qWarning("[Netmix] TcpSession::listen called in non-Disconnected state");
        return false;
    }

    m_pServer = new QTcpServer(this);
    connect(m_pServer, &QTcpServer::newConnection, this, &TcpSession::onNewConnection);

    if (!m_pServer->listen(QHostAddress::Any, m_listenPort)) {
        qWarning("[Netmix] TcpSession::listen failed on port %u: %s",
                 m_listenPort, qPrintable(m_pServer->errorString()));
        delete m_pServer;
        m_pServer = nullptr;
        return false;
    }

    m_selfPeerId = 0;
    setState(Listening);
    return true;
}

void TcpSession::connectToPeer() {
    if (m_state != Disconnected) {
        qWarning("[Netmix] TcpSession::connectToPeer called in non-Disconnected state");
        return;
    }

    m_pSocket = new QTcpSocket(this);
    connect(m_pSocket, &QTcpSocket::connected, this, &TcpSession::onConnected);
    connect(m_pSocket, &QTcpSocket::readyRead, this, &TcpSession::onReadyRead);
    connect(m_pSocket, &QTcpSocket::disconnected, this, &TcpSession::onDisconnected);
    connect(m_pSocket, &QTcpSocket::errorOccurred, this, &TcpSession::onError);

    m_selfPeerId = 1;
    m_pSocket->connectToHost(m_peerAddress, m_peerPort);
    setState(Connecting);
}

void TcpSession::disconnectFromPeer() {
    if (m_state == Disconnected) {
        return;
    }

    if (m_state == Connected || m_state == Degraded) {
        sendBye(QStringLiteral("leaving"));
    }

    if (m_pHeartbeatTimer) {
        m_pHeartbeatTimer->stop();
    }
    if (m_pDeadPeerTimer) {
        m_pDeadPeerTimer->stop();
    }

    cleanupSocket();
    cleanupServer();
    m_readBuffer.clear();
    setState(Disconnected);
}

// ---------------------------------------------------------------------------
// I/O
// ---------------------------------------------------------------------------

void TcpSession::sendMessage(const NetmixMessage& msg) {
    QTcpSocket* sock = activeSocket();
    if (!sock || sock->state() != QAbstractSocket::ConnectedState) {
        qWarning("[Netmix] TcpSession::sendMessage: no connected socket");
        return;
    }

    QByteArray wire = encodeMessage(msg);
    sock->write(wire);
    m_lastSendTime.start();
}

// ---------------------------------------------------------------------------
// State accessors
// ---------------------------------------------------------------------------

TcpSession::State TcpSession::state() const {
    return m_state;
}

quint8 TcpSession::selfPeerId() const {
    return m_selfPeerId;
}

quint8 TcpSession::remotePeerId() const {
    return m_remotePeerId;
}

QString TcpSession::remoteName() const {
    return m_remoteName;
}

QHostAddress TcpSession::peerAddress() const {
    return m_peerAddress;
}

void TcpSession::setLocalUdpPort(quint16 port) {
    m_localUdpPort = port;
}

quint16 TcpSession::localUdpPort() const {
    return m_localUdpPort;
}

quint16 TcpSession::remoteUdpPort() const {
    return m_remoteUdpPort;
}

void TcpSession::setPreassignedChannels(const QVector<quint16>& channels) {
    m_preassignedChannels = channels;
}

const QVector<quint16>& TcpSession::preassignedChannels() const {
    return m_preassignedChannels;
}

QTcpServer* TcpSession::server() const {
    return m_pServer;
}

void TcpSession::injectReceivedData(const QByteArray& data) {
    m_readBuffer.append(data);
    processIncomingData();
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void TcpSession::onNewConnection() {
    if (!m_pServer) {
        return;
    }

    QTcpSocket* clientSocket = m_pServer->nextPendingConnection();
    if (!clientSocket) {
        return;
    }

    if (m_pSocket) {
        qWarning("[Netmix] TcpSession already has a connected socket, rejecting new connection");
        clientSocket->disconnectFromHost();
        clientSocket->deleteLater();
        return;
    }

    m_pSocket = clientSocket;
    m_pSocket->setParent(this);
    m_peerAddress = m_pSocket->peerAddress();
    connect(m_pSocket, &QTcpSocket::readyRead, this, &TcpSession::onReadyRead);
    connect(m_pSocket, &QTcpSocket::disconnected, this, &TcpSession::onDisconnected);

    setState(Handshaking);
    sendHello();
}

void TcpSession::onConnected() {
    setState(Handshaking);
    sendHello();
}

void TcpSession::onReadyRead() {
    QTcpSocket* sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) {
        sock = activeSocket();
    }
    if (!sock) {
        return;
    }

    m_readBuffer.append(sock->readAll());
    processIncomingData();
}

void TcpSession::onDisconnected() {
    if (m_pHeartbeatTimer) {
        m_pHeartbeatTimer->stop();
    }
    if (m_pDeadPeerTimer) {
        m_pDeadPeerTimer->stop();
    }

    cleanupSocket();

    if (m_state != Disconnected) {
        setState(Disconnected);
        emit peerDisconnected();
    }
}

void TcpSession::onError(QAbstractSocket::SocketError error) {
    Q_UNUSED(error);
    qWarning("[Netmix] TcpSession socket error: %s",
             qPrintable(activeSocket() ? activeSocket()->errorString() : QStringLiteral("unknown")));
}

void TcpSession::onHeartbeat() {
    if (m_state != Connected && m_state != Degraded) {
        return;
    }

    // Send Ping heartbeat
    NetmixPing ping;
    ping.sentTick = 0; // Placeholder — clocksync task fills this later

    NetmixMessage msg;
    msg.type = NetmixMessageType::Ping;
    msg.payload = ping;
    sendMessage(msg);
}

void TcpSession::onDeadPeerCheck() {
    if (m_state == Disconnected || m_state == Listening || m_state == Connecting) {
        return;
    }

    qint64 elapsed = m_lastReceiveTime.elapsed();

    if (elapsed >= m_disconnectTimeoutMs) {
        qWarning("[Netmix] Dead peer timeout (%lld ms), disconnecting", elapsed);
        sendBye(QStringLiteral("timeout"));
        disconnectFromPeer();
    } else if (elapsed >= m_degradedTimeoutMs && m_state == Connected) {
        qWarning("[Netmix] Peer degraded timeout (%lld ms)", elapsed);
        setState(Degraded);
    }
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void TcpSession::sendHello() {
    NetmixHello hello;
    hello.peerProtocolVersion = kNetmixProtocolVersion;
    hello.peerName = m_displayName;
    hello.tickRate = m_tickRate;
    hello.rollbackWindow = m_rollbackWindow;
    hello.udpPort = m_localUdpPort;
    hello.preassignedChannels = m_preassignedChannels;

    NetmixMessage msg;
    msg.type = NetmixMessageType::Hello;
    msg.payload = hello;

    sendMessage(msg);

    // Start timers
    if (!m_pHeartbeatTimer) {
        m_pHeartbeatTimer = new QTimer(this);
        connect(m_pHeartbeatTimer, &QTimer::timeout, this, &TcpSession::onHeartbeat);
    }
    if (!m_pDeadPeerTimer) {
        m_pDeadPeerTimer = new QTimer(this);
        connect(m_pDeadPeerTimer, &QTimer::timeout, this, &TcpSession::onDeadPeerCheck);
    }

    m_pHeartbeatTimer->start(kHeartbeatIntervalMs);
    m_pDeadPeerTimer->start(kDeadPeerCheckIntervalMs);

    resetDeadPeerTimer();
}

void TcpSession::processIncomingData() {
    while (true) {
        int bufSize = m_readBuffer.size();
        const int kHeaderLen = 12;

        if (bufSize < kHeaderLen) {
            break;
        }

        // Read magic, version, type, length directly from raw bytes.
        // Wire format (all little-endian): magic(4) + version(2) + type(2) + length(4)
        const unsigned char* raw =
                reinterpret_cast<const unsigned char*>(m_readBuffer.constData());

        quint32 wireMagic = static_cast<quint32>(raw[0]) |
                           (static_cast<quint32>(raw[1]) << 8) |
                           (static_cast<quint32>(raw[2]) << 16) |
                           (static_cast<quint32>(raw[3]) << 24);

        quint16 wireVersion = static_cast<quint16>(raw[4]) |
                              (static_cast<quint16>(raw[5]) << 8);

        quint32 wireLength = static_cast<quint32>(raw[8]) |
                            (static_cast<quint32>(raw[9]) << 8) |
                            (static_cast<quint32>(raw[10]) << 16) |
                            (static_cast<quint32>(raw[11]) << 24);

        if (wireMagic != kNetmixMagic) {
            qWarning("[Netmix] processIncomingData: bad magic 0x%08x", wireMagic);
            sendBye(QStringLiteral("bad magic"));
            disconnectFromPeer();
            return;
        }

        if (wireVersion != kNetmixProtocolVersion) {
            qWarning("[Netmix] processIncomingData: version mismatch %u != %u",
                     wireVersion, kNetmixProtocolVersion);
            sendBye(QStringLiteral("version mismatch"));
            disconnectFromPeer();
            return;
        }

        quint32 frameSize = kHeaderLen + wireLength;

        if (static_cast<quint32>(bufSize) < frameSize) {
            break;
        }

        QByteArray frame(m_readBuffer.constData(), frameSize);
        m_readBuffer.remove(0, frameSize);

        auto decoded = decodeMessage(frame);
        if (!decoded.has_value()) {
            qWarning("[Netmix] processIncomingData: decodeMessage failed on frame (size=%u, wireLen=%u)",
                     frameSize, wireLength);
            sendBye(QStringLiteral("decode failed"));
            disconnectFromPeer();
            return;
        }

        resetDeadPeerTimer();

        const NetmixMessage& msg = *decoded;

        switch (msg.type) {
        case NetmixMessageType::Hello: {
            const auto* hello = std::get_if<NetmixHello>(&msg.payload);
            if (hello) {
                handleHello(*hello);
            }
            break;
        }
        case NetmixMessageType::HelloAck: {
            const auto* ack = std::get_if<NetmixHelloAck>(&msg.payload);
            if (ack) {
                handleHelloAck(*ack);
            }
            break;
        }
        case NetmixMessageType::Bye: {
            const auto* bye = std::get_if<NetmixBye>(&msg.payload);
            if (bye) {
                handleBye(*bye);
            }
            return; // handleBye disconnects us — no further processing
        }
        default:
            emit messageReceived(msg);
            break;
        }
    }
}

void TcpSession::handleHello(const NetmixHello& hello) {
    if (hello.peerProtocolVersion != kNetmixProtocolVersion) {
        qWarning("[Netmix] handleHello: peer protocol version %u != %u, rejecting",
                 hello.peerProtocolVersion, kNetmixProtocolVersion);
        if (activeSocket()) {
            sendBye(QStringLiteral("version mismatch"));
        }
        disconnectFromPeer();
        return;
    }

    m_remoteName = hello.peerName;
    m_remoteUdpPort = hello.udpPort;
    m_remotePreassignedChannels = hello.preassignedChannels;
    m_remotePeerId = (m_selfPeerId == 0) ? 1 : 0;

    emit helloReceived(m_remoteUdpPort, m_remoteName);
    emit helloComplete(m_remotePeerId, m_remotePreassignedChannels);

    // Only send HelloAck if we have a live socket (not injectReceivedData path)
    if (!activeSocket()) {
        return;
    }

    NetmixHelloAck ack;
    ack.peerProtocolVersion = kNetmixProtocolVersion;
    ack.peerName = m_displayName;
    ack.peerId = (m_selfPeerId == 0) ? 1 : 0;
    ack.tickRate = hello.tickRate;
    ack.rollbackWindow = hello.rollbackWindow;
    ack.preassignedChannels = m_preassignedChannels;

    NetmixMessage msg;
    msg.type = NetmixMessageType::HelloAck;
    msg.payload = ack;
    sendMessage(msg);
}

void TcpSession::handleHelloAck(const NetmixHelloAck& ack) {
    // ack.peerId confirms the assigned peerId of the receiver (reflected in memory.md).
    // The remote peer is always the other role, derived from selfPeerId.
    m_remotePeerId = (m_selfPeerId == 0) ? 1 : 0;
    m_remotePreassignedChannels = ack.preassignedChannels;

    emit helloAckReceived(ack.initiatorTick);
    emit helloComplete(m_remotePeerId, m_remotePreassignedChannels);

    if (m_state == Handshaking) {
        setState(Connected);
    }
}

void TcpSession::handleBye(const NetmixBye& bye) {
    qWarning("[Netmix] Peer sent Bye: %s", qPrintable(bye.reason));
    disconnectFromPeer();
}

void TcpSession::sendBye(const QString& reason) {
    NetmixBye bye;
    bye.reason = reason;

    NetmixMessage msg;
    msg.type = NetmixMessageType::Bye;
    msg.payload = bye;

    QTcpSocket* sock = activeSocket();
    if (sock && sock->state() == QAbstractSocket::ConnectedState) {
        QByteArray wire = encodeMessage(msg);
        sock->write(wire);
        sock->flush();
    }
}

void TcpSession::setState(State state) {
    if (m_state != state) {
        m_state = state;
        emit stateChanged(m_state);
    }
}

void TcpSession::resetDeadPeerTimer() {
    m_lastReceiveTime.start();
    // If we were Degraded and received traffic, go back to Connected
    if (m_state == Degraded) {
        setState(Connected);
    }
}

void TcpSession::cleanupSocket() {
    if (m_pSocket) {
        m_pSocket->disconnect(this);
        m_pSocket->abort();
        m_pSocket->deleteLater();
        m_pSocket = nullptr;
    }
}

void TcpSession::cleanupServer() {
    if (m_pServer) {
        m_pServer->close();
        delete m_pServer;
        m_pServer = nullptr;
    }
}

QTcpSocket* TcpSession::activeSocket() const {
    return m_pSocket;
}

#include "moc_tcpsession.cpp"
