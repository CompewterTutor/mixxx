#pragma once

#include <QObject>

#include "netmix/sessionclock.h"

class ControlObject;

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

    void hostSession();
    void joinSession();
    void leaveSession();

    SessionState state() const { return m_state; }

  signals:
    void sessionStateChanged(SessionState newState);

  private:
    void setState(SessionState state);

    SessionState m_state = Idle;
    SessionClock m_sessionClock;
    ControlObject* m_pStatusCO;
};
