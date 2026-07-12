#pragma once

#include <QObject>
#include <QVector>

#include "control/controlproxy.h"
#include "netmix/controlallowlist.h"

class SessionClock;

class ControlCapture : public QObject {
    Q_OBJECT
  public:
    explicit ControlCapture(QObject* parent = nullptr);
    ~ControlCapture() override;

    void start(const SessionClock* clock);
    void stop();

    const QVector<ControlProxy*>& proxies() const { return m_proxies; }

  signals:
    void captured(quint32 tick, quint16 wireId, double value);

  private:
    void onProxyValueChange(quint16 wireId, double value);

    const SessionClock* m_pClock = nullptr;
    QVector<ControlProxy*> m_proxies;
    QVector<AllowlistEntry> m_entries;
};
