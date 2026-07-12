#pragma once

#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <array>

#include "netmix/protocol.h"
#include "netmix/sessionclock.h"

class ClockSync : public QObject {
    Q_OBJECT
  public:
    static constexpr int kPingIntervalMs = 250;
    static constexpr int kMedianWindowSize = 16;
    static constexpr int kUpdateEveryNPongs = 4;

    explicit ClockSync(SessionClock* pClock, QObject* parent = nullptr);
    ~ClockSync() override;

    void start(quint16 localPort, QHostAddress peerAddr, quint16 peerPort);
    void startTestMode();
    void stop();
    void setInitialOffset(quint32 hostTick, quint32 localTick);

    // Test injection — processes incoming message directly (bypasses UDP)
    void injectMessage(const NetmixMessage& msg);

    // Send a Ping now (used by tests, also by timer)
    void sendPingNow();

    // State accessors
    double smoothedRttMs() const { return m_smoothedRttMs; }
    qint32 currentOffset() const { return m_currentOffset; }
    int medianFilterSize() const { return m_sampleCount; }
    bool isRunning() const { return m_running; }
    bool hasPendingPong() const { return m_pendingPong; }

  signals:
    void offsetUpdated(qint32 offset);
    void rttUpdated(double rttMs);
    void outgoingMessage(const NetmixMessage& msg);

  private slots:
    void onSendPing();
    void onReadyRead();

  private:
    void handlePing(const NetmixPing& ping);
    void handlePong(const NetmixPong& pong);
    void updateFilter(qint32 sample);
    qint32 computeMedian() const;
    void sendMessage(const NetmixMessage& msg);

    SessionClock* m_pClock;

    // Network
    QUdpSocket* m_pSocket = nullptr;
    QHostAddress m_peerAddress;
    quint16 m_peerPort = 21200;
    bool m_testMode = false;
    bool m_running = false;

    // Ping state
    QTimer* m_pPingTimer = nullptr;
    quint32 m_lastSentTick = 0;
    quint64 m_lastSentTimeUsec = 0;
    bool m_pendingPong = false;
    QElapsedTimer m_elapsed;
    quint32 m_seqNumber = 0;

    // Median filter
    std::array<qint32, kMedianWindowSize> m_offsetSamples{};
    int m_sampleCount = 0;
    int m_nextSlot = 0;
    qint32 m_currentOffset = 0;

    // RTT
    double m_smoothedRttMs = 0.0;
    int m_pongCount = 0;
};
