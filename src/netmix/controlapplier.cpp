#include "netmix/controlapplier.h"

#include <QtGlobal>

#include "util/logger.h"

namespace {
const mixxx::Logger kLogger("ControlApplier");
} // namespace

ControlApplier::ControlApplier(QObject* parent)
        : QObject(parent) {}

ControlApplier::~ControlApplier() = default;

void ControlApplier::setProxies(const QVector<ControlProxy*>& proxies) {
    m_proxies = proxies;
    m_wireToProxyIdx.clear();
    m_ramps.clear();

    const auto& entries = ControlAllowlist::entries();
    for (int i = 0; i < entries.size(); ++i) {
        if (i < m_proxies.size()) {
            m_wireToProxyIdx.insert(entries[i].wireId, i);
        }
    }

    // Pre-allocate ramp slots for continuous controls
    int continuousCount = 0;
    for (const auto& entry : entries) {
        if (entry.kind == ControlKind::Continuous) {
            ++continuousCount;
        }
    }
    m_ramps.reserve(continuousCount);

    kLogger.info() << "Applier initialized:" << m_wireToProxyIdx.size() << "controls";
}

int ControlApplier::findProxyForWireId(quint16 wireId) const {
    auto it = m_wireToProxyIdx.constFind(wireId);
    if (it != m_wireToProxyIdx.constEnd()) {
        return it.value();
    }
    return -1;
}

void ControlApplier::apply(quint16 wireId, double value) {
    int idx = findProxyForWireId(wireId);
    if (idx < 0 || idx >= m_proxies.size()) {
        qWarning("[Netmix] ControlApplier::apply — unknown wireId %u", wireId);
        return;
    }

    // Cancel any in-flight ramp for this wire
    for (auto& ramp : m_ramps) {
        if (ramp.proxyIdx == idx) {
            ramp.remainingTicks = 0;
        }
    }

    m_proxies[idx]->set(value);
}

void ControlApplier::applyRamped(quint16 wireId, double target, int ticks) {
    int idx = findProxyForWireId(wireId);
    if (idx < 0 || idx >= m_proxies.size()) {
        qWarning("[Netmix] ControlApplier::applyRamped — unknown wireId %u", wireId);
        return;
    }

    // Check if this control should be ramped (only Continuous kind)
    auto kindOpt = ControlAllowlist::kindForWireId(wireId);
    if (!kindOpt.has_value() || kindOpt.value() != ControlKind::Continuous) {
        // Discrete or Seek: apply directly without ramping
        m_proxies[idx]->set(target);
        return;
    }

    // Supersede existing ramp for this proxy
    bool found = false;
    for (auto& ramp : m_ramps) {
        if (ramp.proxyIdx == idx) {
            ramp.startVal = m_proxies[idx]->get();
            ramp.targetVal = target;
            ramp.remainingTicks = qMax(ticks, 1);
            ramp.totalTicks = qMax(ticks, 1);
            found = true;
            break;
        }
    }

    if (!found) {
        m_ramps.push_back({idx, m_proxies[idx]->get(), target,
                qMax(ticks, 1), qMax(ticks, 1)});
    }
}

void ControlApplier::advanceTick() {
    for (int i = m_ramps.size() - 1; i >= 0; --i) {
        auto& ramp = m_ramps[i];
        if (ramp.remainingTicks <= 0) {
            continue;
        }

        --ramp.remainingTicks;
        double current;
        if (ramp.remainingTicks <= 0) {
            current = ramp.targetVal;
        } else {
            double fraction = static_cast<double>(ramp.remainingTicks) /
                    static_cast<double>(ramp.totalTicks);
            current = ramp.targetVal +
                    (ramp.startVal - ramp.targetVal) * fraction;
        }

        if (ramp.proxyIdx >= 0 && ramp.proxyIdx < m_proxies.size()) {
            m_proxies[ramp.proxyIdx]->set(current);
        }

        if (ramp.remainingTicks <= 0) {
            m_ramps.removeAt(i);
        }
    }
}

#include "moc_controlapplier.cpp"
