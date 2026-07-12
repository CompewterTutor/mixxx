#pragma once

#include <QObject>
#include <QVector>

#include "control/controlproxy.h"
#include "netmix/controlallowlist.h"

class ChannelOwnership;

class ControlApplier : public QObject {
    Q_OBJECT
  public:
    explicit ControlApplier(QObject* parent = nullptr);
    ~ControlApplier() override;

    void setProxies(const QVector<ControlProxy*>& proxies);
    void setOwnership(ChannelOwnership* ownership) { m_pOwnership = ownership; }
    void setOwnershipFilterEnabled(bool enabled) { m_ownershipFilterEnabled = enabled; }
    void apply(quint16 wireId, double value);
    void applyRamped(quint16 wireId, double target, int ticks);
    void advanceTick();

    bool isActive() const { return !m_proxies.empty(); }

  private:
    struct RampEntry {
        int proxyIdx = -1;
        double startVal = 0.0;
        double targetVal = 0.0;
        int remainingTicks = 0;
        int totalTicks = 1;
    };

    int findProxyForWireId(quint16 wireId) const;

    // wireId -> proxy index lookup
    QHash<quint16, int> m_wireToProxyIdx;

    // Same proxies owned by ControlCapture — raw pointers, not owned
    QVector<ControlProxy*> m_proxies;

    // Ownership filtering
    ChannelOwnership* m_pOwnership = nullptr;
    bool m_ownershipFilterEnabled = true;

    // Active ramps (one per continuous control max)
    QVector<RampEntry> m_ramps;
};
