#pragma once

#include <QObject>
#include <QVector>

#include "control/controlproxy.h"
#include "netmix/controlallowlist.h"

class ChannelOwnership;
class SessionClock;

class ControlCapture : public QObject {
    Q_OBJECT
  public:
    explicit ControlCapture(QObject* parent = nullptr);
    ~ControlCapture() override;

    void start(const SessionClock* clock);
    void stop();

    void setMuted(bool muted) { m_muted = muted; }
    bool isMuted() const { return m_muted; }

    void setOwnership(ChannelOwnership* ownership) { m_pOwnership = ownership; }
    const QVector<ControlProxy*>& proxies() const { return m_proxies; }

  signals:
    void captured(quint32 tick, quint16 wireId, double value);

  private:
    void onProxyValueChange(quint16 wireId, double value);

    bool m_muted = false;
    const SessionClock* m_pClock = nullptr;
    ChannelOwnership* m_pOwnership = nullptr;
    QVector<ControlProxy*> m_proxies;
    QVector<AllowlistEntry> m_entries;
};
