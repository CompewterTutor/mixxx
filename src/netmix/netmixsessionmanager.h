#pragma once

#include <QHostAddress>
#include <QObject>
#include <QVector>

#include "netmix/controlapplier.h"
#include "netmix/controlcapture.h"
#include "netmix/inputframe.h"
#include "netmix/protocol.h"
#include "netmix/quantizer.h"
#include "netmix/sessionclock.h"
#include "netmix/tcpsession.h"
#include "netmix/udpchannel.h"

class ControlObject;
class ControlProxy;

class NetmixSessionManager : public QObject {
    Q_OBJECT
  public:
    enum SessionState {
        Idle = 0,
        Connecting = 1,
        Connected = 2,
        Degraded = 3,
    };
    Q_ENUM(SessionState)

    NetmixSessionManager(QObject* parent = nullptr);
    ~NetmixSessionManager() override;

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    void hostSession(quint16 port);
    void joinSession(const QHostAddress& address, quint16 port);
    void leaveSession();

    SessionState state() const { return m_state; }

    // Test accessors
    TcpSession* tcpSession() const { return m_pTcpSession; }
    UdpChannel* udpChannel() const { return m_pUdpChannel; }
    SessionClock& sessionClock() { return m_sessionClock; }

  signals:
    void sessionStateChanged(SessionState newState);

  private slots:
    void onTcpStateChanged(TcpSession::State ts);
    void onTickAdvanced(quint32 tick);
    void onInputFrameReceived(quint32 baseTick,
            QVector<NetmixInputFrameEvent> events);

  private:
    void setState(SessionState state);
    void onTcpConnected();
    void onTcpDisconnected();
    void deleteSubComponents();

    SessionState m_state = Idle;
    bool m_enabled = false;
    SessionClock m_sessionClock;
    ControlObject* m_pStatusCO;

    // Session sub-components (created on hostSession/joinSession)
    TcpSession* m_pTcpSession = nullptr;
    UdpChannel* m_pUdpChannel = nullptr;
    ControlCapture* m_pCapture = nullptr;
    ControlApplier* m_pApplier = nullptr;
    InputFramePacker* m_pPacker = nullptr;
    NetmixQuantizer* m_pQuantizer = nullptr;
    ControlObject* m_pQuantizeCO = nullptr;
    ControlProxy* m_pBpmProxy = nullptr;
};
