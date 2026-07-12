#include "netmix/rollbackengine.h"

#include <limits>
#include <QSet>

#include <QtGlobal>

#include "util/logger.h"

namespace {
const mixxx::Logger kLogger("RollbackEngine");

inline bool tickLt(quint32 a, quint32 b) {
    return static_cast<qint32>(a - b) < 0;
}
inline bool tickLe(quint32 a, quint32 b) {
    return static_cast<qint32>(a - b) <= 0;
}
inline bool tickGt(quint32 a, quint32 b) {
    return static_cast<qint32>(a - b) > 0;
}

} // anonymous namespace

RollbackEngine::RollbackEngine(QObject* parent)
        : QObject(parent) {}

RollbackEngine::~RollbackEngine() {
    qDeleteAll(m_proxies);
    m_proxies.clear();
}

void RollbackEngine::setWindowSize(int ticks) {
    Q_ASSERT(ticks >= 2 && ticks <= kMaxWindow);
    m_windowSize = ticks;
    m_snapshots.clear();
    m_hasSnapshots = false;
    m_loggedWindowExceeded = false;
}

void RollbackEngine::initialize() {
    m_snapshots.resize(m_windowSize);
    for (auto& snap : m_snapshots) {
        snap.occupied = false;
    }
    m_hasSnapshots = false;
    m_oldestSnapshotTick = 0;
    m_newestSnapshotTick = 0;
    m_rollbackCount = 0;
    m_windowExceededCount = 0;
    m_loggedWindowExceeded = false;

    m_entries = ControlAllowlist::entries();
    m_proxies.reserve(m_entries.size());
    m_wireToEntryIdx.clear();
    for (int i = 0; i < m_entries.size(); ++i) {
        auto* proxy = new ControlProxy(m_entries[i].key, this);
        m_proxies.append(proxy);
        m_wireToEntryIdx.insert(m_entries[i].wireId, i);
    }

    takeSnapshot(0);

    kLogger.info() << "RollbackEngine initialized: window=" << m_windowSize
                   << "controls=" << m_proxies.size();
}

int RollbackEngine::snapshotSlotForTick(quint32 tick) const {
    return static_cast<int>(tick % m_windowSize);
}

void RollbackEngine::takeSnapshot(quint32 tick) {
    Q_ASSERT(m_snapshots.size() == m_windowSize);

    int slot = snapshotSlotForTick(tick);
    Snapshot& snap = m_snapshots[slot];

    snap.tick = tick;
    snap.occupied = true;
    snap.values.resize(m_proxies.size());
    for (int i = 0; i < m_proxies.size(); ++i) {
        snap.values[i] = m_proxies[i]->get();
    }

    bool foundNewest = false;
    bool foundOldest = false;
    quint32 scanNewest = 0;
    quint32 scanOldest = 0;
    for (int i = 0; i < m_windowSize; ++i) {
        if (!m_snapshots[i].occupied) {
            continue;
        }
        quint32 st = m_snapshots[i].tick;
        if (!foundNewest || tickGt(st, scanNewest)) {
            scanNewest = st;
            foundNewest = true;
        }
        if (!foundOldest || tickLt(st, scanOldest)) {
            scanOldest = st;
            foundOldest = true;
        }
    }

    m_hasSnapshots = foundNewest && foundOldest;
    if (m_hasSnapshots) {
        m_newestSnapshotTick = scanNewest;
        m_oldestSnapshotTick = scanOldest;
    }
}

void RollbackEngine::restoreSnapshot(const Snapshot& snap) {
    Q_ASSERT(snap.values.size() == m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        m_pApplier->apply(m_entries[i].wireId, snap.values[i]);
    }
}

void RollbackEngine::onTick(quint32 tick) {
    if (!m_pBuffer || !m_pApplier) {
        return;
    }

    m_pApplier->advanceTick();
    takeSnapshot(tick);

    auto divergent = m_pBuffer->firstDivergentTick();
    if (!divergent.has_value()) {
        m_loggedWindowExceeded = false;
        return;
    }

    quint32 divTick = divergent.value();

    if (m_lastHandledDivTick > 0 && tickLe(divTick, m_lastHandledDivTick)) {
        return;
    }

    quint32 windowEdge =
            tickGt(tick, static_cast<quint32>(m_windowSize))
                    ? tick - static_cast<quint32>(m_windowSize)
                    : 0;

    if (tickLt(divTick, windowEdge)) {
        if (!m_loggedWindowExceeded) {
            qWarning() << "[Netmix] RollbackEngine window exceeded:"
                       << "divergentTick=" << divTick
                       << "currentTick=" << tick
                       << "window=" << m_windowSize;
            m_loggedWindowExceeded = true;
            emit windowExceeded(divTick);
        }
        ++m_windowExceededCount;
        m_lastHandledDivTick = tick;

        if (m_pBuffer->isRemoteConfirmed(divTick)) {
            auto frame = m_pBuffer->remoteFrameAt(divTick);
            for (const auto& evt : frame.events) {
                m_pApplier->apply(evt.wireId, evt.value);
            }
        }
        return;
    }

    m_loggedWindowExceeded = false;

    if (divTick == 0) {
        qWarning() << "[Netmix] RollbackEngine cannot rollback at tick 0";
        return;
    }

    quint32 restoreTick = divTick - 1;
    const Snapshot* restoreSnap = nullptr;

    int bestDist = std::numeric_limits<int>::max();
    int bestSlot = -1;
    for (int i = 0; i < m_windowSize; ++i) {
        if (!m_snapshots[i].occupied) {
            continue;
        }
        quint32 sTick = m_snapshots[i].tick;
        if (tickLe(sTick, restoreTick)) {
            int dist = static_cast<int>(restoreTick - sTick);
            if (dist < bestDist) {
                bestDist = dist;
                bestSlot = i;
            }
        }
    }

    if (bestSlot >= 0) {
        restoreSnap = &m_snapshots[bestSlot];
    }

    if (!restoreSnap) {
        kLogger.warning() << "No snapshot for restore at tick" << restoreTick;
        return;
    }

    kLogger.info() << "Rollback: tick" << restoreSnap->tick
                   << "->" << tick
                   << "divergent at" << divTick;
    ++m_rollbackCount;

    restoreSnapshot(*restoreSnap);

    QSet<quint16> rampedWireIds;

    for (quint32 t = divTick; tickLe(t, tick); ++t) {
        if (m_pBuffer->isRemoteConfirmed(t)) {
            auto frame = m_pBuffer->remoteFrameAt(t);
            for (const auto& evt : frame.events) {
                auto kindOpt = ControlAllowlist::kindForWireId(evt.wireId);
                if (kindOpt.has_value() && kindOpt.value() == ControlKind::Continuous) {
                    double current = 0.0;
                    auto idxIt = m_wireToEntryIdx.constFind(evt.wireId);
                    if (idxIt != m_wireToEntryIdx.constEnd() &&
                            idxIt.value() < m_proxies.size()) {
                        current = m_proxies[idxIt.value()]->get();
                    }
                    int tickCount = rampTicksForCorrection(evt.value, current);
                    m_pApplier->applyRamped(evt.wireId, evt.value, tickCount);
                    rampedWireIds.insert(evt.wireId);
                } else {
                    m_pApplier->apply(evt.wireId, evt.value);
                    rampedWireIds.remove(evt.wireId);
                }
            }
        }

        if (m_pBuffer->hasLocal(t)) {
            auto frame = m_pBuffer->localFrameAt(t);
            for (const auto& evt : frame.events) {
                m_pApplier->apply(evt.wireId, evt.value);
                rampedWireIds.remove(evt.wireId);
            }
        }

        if (!m_pBuffer->isRemoteConfirmed(t) && m_pPrediction) {
            auto predicted = m_pPrediction->predict(t, *m_pBuffer);
            for (const auto& evt : predicted.events) {
                if (!rampedWireIds.contains(evt.wireId)) {
                    m_pApplier->apply(evt.wireId, evt.value);
                }
            }
        }
    }

    emit rollbackPerformed(restoreSnap->tick, tick);
    m_lastHandledDivTick = tick;
}

int RollbackEngine::rampTicksForCorrection(double target, double current) const {
    double delta = qAbs(target - current);
    int ticks = static_cast<int>(delta * kRampScale);
    return qBound(1, ticks, kMaxRampTicks);
}

#include "moc_rollbackengine.cpp"
