#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include "netmix/protocol.h"

class TcpSession : public QObject {
    Q_OBJECT
  public:
    enum State {
        Disconnected = 0,
        Listening,
        Connecting,
        Handshaking,
        Connected,
        Degraded,
    };
    Q_ENUM(State)

    explicit TcpSession(QObject* parent = nullptr);
    ~TcpSession() override;

    // Configuration
    void setListenPort(quint16 port);
    void setPeerAddress(const QHostAddress& address, quint16 port);
    void setDisplayName(const QString& name);
    void setSessionParams(quint16 tickRate, quint16 rollbackWindow);
    void setTimeoutsForTest(int degradedSec, int disconnectSec);

    // Lifecycle
    bool listen();
    void connectToPeer();
    void disconnectFromPeer();

    // I/O
    void sendMessage(const NetmixMessage& msg);

    // State accessors
    State state() const;
    quint8 selfPeerId() const;
    quint8 remotePeerId() const;
    QString remoteName() const;
    QHostAddress peerAddress() const;

    // UDP port plumbing
    void setLocalUdpPort(quint16 port);
    quint16 localUdpPort() const;
    quint16 remoteUdpPort() const;

    // Pre-assigned channels
    void setPreassignedChannels(const QVector<quint16>& channels);
    const QVector<quint16>& preassignedChannels() const;

    // Test helpers
    QTcpServer* server() const;
    void injectReceivedData(const QByteArray& data);

  signals:
    void stateChanged(TcpSession::State newState);
    void messageReceived(const NetmixMessage& msg);
    void peerDisconnected();
    void helloReceived(quint16 udpPort, QString peerName);
    void helloAckReceived(quint32 initiatorTick);
    void helloComplete(quint8 peerId, const QVector<quint16>& remotePreassigned);

  private slots:
    void onNewConnection();
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError error);
    void onHeartbeat();
    void onDeadPeerCheck();

  private:
    void sendHello();
    void processIncomingData();
    void handleHello(const NetmixHello& hello);
    void handleHelloAck(const NetmixHelloAck& ack);
    void handleBye(const NetmixBye& bye);
    void sendBye(const QString& reason);
    void setState(State state);
    void resetDeadPeerTimer();
    void cleanupSocket();
    void cleanupServer();

    QTcpSocket* activeSocket() const;

    // Config
    quint16 m_listenPort = 21200;
    QHostAddress m_peerAddress;
    quint16 m_peerPort = 21200;
    QString m_displayName;
    quint16 m_tickRate = 240;
    quint16 m_rollbackWindow = 8;

    // Timeouts (ms)
    int m_degradedTimeoutMs = 5000;
    int m_disconnectTimeoutMs = 15000;

    // UDP port plumbing
    quint16 m_localUdpPort = 0;
    quint16 m_remoteUdpPort = 0;

    // Pre-assigned channels
    QVector<quint16> m_preassignedChannels;
    QVector<quint16> m_remotePreassignedChannels;

    // State
    State m_state = Disconnected;
    quint8 m_selfPeerId = 0;
    quint8 m_remotePeerId = 0;
    QString m_remoteName;

    // Network
    QTcpServer* m_pServer = nullptr;
    QTcpSocket* m_pSocket = nullptr;
    QByteArray m_readBuffer;

    // Timers
    QTimer* m_pHeartbeatTimer = nullptr;
    QTimer* m_pDeadPeerTimer = nullptr;
    QElapsedTimer m_lastReceiveTime;
    QElapsedTimer m_lastSendTime;
};
