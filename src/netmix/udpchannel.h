#pragma once

#include <QHostAddress>
#include <QObject>
#include <QUdpSocket>
#include <QSet>
#include <QVector>

#include "netmix/protocol.h"

class UdpChannel : public QObject {
    Q_OBJECT
  public:
    static constexpr int kWindowSize = 64;

    explicit UdpChannel(QObject* parent = nullptr);
    ~UdpChannel() override;

    void setPeer(const QHostAddress& address, quint16 port);
    bool bind(quint16 port);
    void sendFrames(const QVector<NetmixInputFrame>& frames);
    void disconnectFromPeer();

    // Stats
    quint64 sentCount() const { return m_sentCount; }
    quint64 receivedCount() const { return m_receivedCount; }
    quint64 droppedCount() const { return m_droppedCount; }
    quint64 outOfOrderCount() const { return m_outOfOrderCount; }

    // Test helpers
    QUdpSocket* socket() const { return m_pSocket; }

  signals:
    void inputFrameReceived(quint32 baseTick,
            QVector<NetmixInputFrameEvent> events);

  private slots:
    void onReadyRead();

  private:
    bool isStale(quint32 seq) const;
    void pruneReceivedSeqs();

    QUdpSocket* m_pSocket = nullptr;

    QHostAddress m_peerAddress;
    quint16 m_peerPort = 21200;

    quint32 m_sequenceNumber = 0;
    quint32 m_highestReceived = 0;
    QSet<quint32> m_receivedSeqs;

    quint64 m_sentCount = 0;
    quint64 m_receivedCount = 0;
    quint64 m_droppedCount = 0;
    quint64 m_outOfOrderCount = 0;
};
