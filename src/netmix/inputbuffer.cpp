#include "netmix/inputbuffer.h"

#include <QDebug>
#include <QtGlobal>

namespace {

// Modular tick comparison (window guaranteed < 2^31)
inline bool tickLt(quint32 a, quint32 b) {
    return static_cast<qint32>(a - b) < 0;
}
inline bool tickLe(quint32 a, quint32 b) {
    return static_cast<qint32>(a - b) <= 0;
}
inline bool tickGt(quint32 a, quint32 b) {
    return static_cast<qint32>(a - b) > 0;
}
inline bool tickGe(quint32 a, quint32 b) {
    return static_cast<qint32>(a - b) >= 0;
}

} // anonymous namespace

InputBuffer::InputBuffer(QObject* parent)
        : QObject(parent) {
    m_remoteRing.resize(kDefaultCapacity);
    m_localRing.resize(kDefaultCapacity);
    clear();
}

InputBuffer::~InputBuffer() = default;

void InputBuffer::setCapacity(int ticks) {
    Q_ASSERT(ticks > 0);
    m_capacity = ticks;
    m_remoteRing.resize(ticks);
    m_localRing.resize(ticks);
    clear();
}

int InputBuffer::slotForTick(quint32 tick) const {
    return static_cast<int>(tick % m_capacity);
}

void InputBuffer::updateWindow(quint32 tick) {
    if (!m_hasEntries) {
        m_oldestTick = tick;
        m_newestTick = tick;
        m_hasEntries = true;
        return;
    }

    if (tickLe(tick, m_newestTick)) {
        return; // not newer
    }

    m_newestTick = tick;
    quint32 windowEdge = tick - static_cast<quint32>(m_capacity) + 1;
    if (tickLt(m_oldestTick, windowEdge)) {
        m_oldestTick = windowEdge;
        evictStaleSlots();
    }
}

void InputBuffer::evictStaleSlots() {
    for (int i = 0; i < m_capacity; ++i) {
        RemoteTickSlot& rslot = m_remoteRing[i];
        if (rslot.occupied && tickLt(rslot.tick, m_oldestTick)) {
            rslot = {};
        }
        LocalTickSlot& lslot = m_localRing[i];
        if (lslot.occupied && tickLt(lslot.tick, m_oldestTick)) {
            lslot = {};
        }
    }
}

void InputBuffer::insertRemoteConfirmed(quint32 tick, const NetmixInputFrame& frame) {
    int idx = slotForTick(tick);
    RemoteTickSlot& slot = m_remoteRing[idx];

    if (slot.occupied && slot.tick != tick) {
        if (tickGe(slot.tick, m_oldestTick) && tickLe(slot.tick, m_newestTick)) {
            qWarning() << "[Netmix] InputBuffer ring collision at index" << idx
                       << "tick" << slot.tick << "!= requested" << tick;
        }
    }

    bool divergent = false;
    if (slot.predicted) {
        divergent = eventsDiffer(slot.events, slot.eventCount,
                frame.events.constData(), frame.events.size());
    }

    slot.tick = tick;
    slot.occupied = true;
    slot.confirmed = true;
    slot.predicted = false;
    slot.divergent = divergent;

    int copyCount = qMin(frame.events.size(), kMaxEventsPerTick);
    for (int i = 0; i < copyCount; ++i) {
        slot.events[i] = frame.events[i];
    }
    slot.eventCount = copyCount;

    if (divergent) {
        if (!m_cachedFirstDivergentTick.has_value() ||
                tickLt(tick, m_cachedFirstDivergentTick.value())) {
            m_cachedFirstDivergentTick = tick;
        }
    }

    updateWindow(tick);
}

void InputBuffer::insertRemotePredicted(quint32 tick, const NetmixInputFrame& frame) {
    int idx = slotForTick(tick);
    RemoteTickSlot& slot = m_remoteRing[idx];

    if (slot.occupied && slot.confirmed) {
        return;
    }

    if (slot.occupied && slot.tick != tick) {
        if (tickGe(slot.tick, m_oldestTick) && tickLe(slot.tick, m_newestTick)) {
            qWarning() << "[Netmix] InputBuffer ring collision at index" << idx
                       << "tick" << slot.tick << "!= requested" << tick;
        }
    }

    slot.tick = tick;
    slot.occupied = true;
    slot.confirmed = false;
    slot.predicted = true;
    slot.divergent = false;

    int copyCount = qMin(frame.events.size(), kMaxEventsPerTick);
    for (int i = 0; i < copyCount; ++i) {
        slot.events[i] = frame.events[i];
    }
    slot.eventCount = copyCount;

    updateWindow(tick);
}

void InputBuffer::insertLocal(quint32 tick, const NetmixInputFrame& frame) {
    int idx = slotForTick(tick);
    LocalTickSlot& slot = m_localRing[idx];

    if (slot.occupied && slot.tick != tick) {
        if (tickGe(slot.tick, m_oldestTick) && tickLe(slot.tick, m_newestTick)) {
            qWarning() << "[Netmix] InputBuffer local ring collision at index" << idx
                       << "tick" << slot.tick << "!= requested" << tick;
        }
    }

    slot.tick = tick;
    slot.occupied = true;

    int copyCount = qMin(frame.events.size(), kMaxEventsPerTick);
    for (int i = 0; i < copyCount; ++i) {
        slot.events[i] = frame.events[i];
    }
    slot.eventCount = copyCount;

    updateWindow(tick);
}

bool InputBuffer::hasRemote(quint32 tick) const {
    int idx = slotForTick(tick);
    const RemoteTickSlot& slot = m_remoteRing[idx];
    return slot.occupied && slot.tick == tick;
}

bool InputBuffer::isRemoteConfirmed(quint32 tick) const {
    int idx = slotForTick(tick);
    const RemoteTickSlot& slot = m_remoteRing[idx];
    return slot.occupied && slot.tick == tick && slot.confirmed;
}

bool InputBuffer::hasLocal(quint32 tick) const {
    int idx = slotForTick(tick);
    const LocalTickSlot& slot = m_localRing[idx];
    return slot.occupied && slot.tick == tick;
}

NetmixInputFrame InputBuffer::remoteFrameAt(quint32 tick) const {
    NetmixInputFrame result;
    int idx = slotForTick(tick);
    const RemoteTickSlot& slot = m_remoteRing[idx];

    if (!slot.occupied || slot.tick != tick) {
        return result;
    }

    result.baseTick = tick;
    result.events.reserve(slot.eventCount);
    for (int i = 0; i < slot.eventCount; ++i) {
        result.events.append(slot.events[i]);
    }
    return result;
}

NetmixInputFrame InputBuffer::localFrameAt(quint32 tick) const {
    NetmixInputFrame result;
    int idx = slotForTick(tick);
    const LocalTickSlot& slot = m_localRing[idx];

    if (!slot.occupied || slot.tick != tick) {
        return result;
    }

    result.baseTick = tick;
    result.events.reserve(slot.eventCount);
    for (int i = 0; i < slot.eventCount; ++i) {
        result.events.append(slot.events[i]);
    }
    return result;
}

void InputBuffer::clear() {
    for (int i = 0; i < m_capacity; ++i) {
        m_remoteRing[i] = {};
        m_localRing[i] = {};
    }
    m_oldestTick = 0;
    m_newestTick = 0;
    m_hasEntries = false;
    m_cachedFirstDivergentTick.reset();
}

void InputBuffer::advanceWindow(quint32 newEarliestTick) {
    if (!m_hasEntries) {
        return;
    }

    m_oldestTick = newEarliestTick;
    evictStaleSlots();

    bool found = false;
    quint32 newNewest = 0;
    for (int i = 0; i < m_capacity; ++i) {
        if (m_remoteRing[i].occupied) {
            if (!found || tickGt(m_remoteRing[i].tick, newNewest)) {
                newNewest = m_remoteRing[i].tick;
                found = true;
            }
        }
        if (m_localRing[i].occupied) {
            if (!found || tickGt(m_localRing[i].tick, newNewest)) {
                newNewest = m_localRing[i].tick;
                found = true;
            }
        }
    }

    m_newestTick = newNewest;
    m_hasEntries = found;

    if (!found) {
        m_cachedFirstDivergentTick.reset();
    }
}

std::optional<quint32> InputBuffer::firstDivergentTick() const {
    if (m_cachedFirstDivergentTick.has_value()) {
        return m_cachedFirstDivergentTick;
    }

    if (!m_hasEntries) {
        return std::nullopt;
    }

    std::optional<quint32> result;
    for (int i = 0; i < m_capacity; ++i) {
        const RemoteTickSlot& slot = m_remoteRing[i];
        if (!slot.occupied || !slot.divergent) {
            continue;
        }
        if (tickGe(slot.tick, m_oldestTick) && tickLe(slot.tick, m_newestTick)) {
            if (!result.has_value() || tickLt(slot.tick, result.value())) {
                result = slot.tick;
            }
        }
    }

    return result;
}

bool InputBuffer::eventsDiffer(
        const NetmixInputFrameEvent* a, int aCount,
        const NetmixInputFrameEvent* b, int bCount) {
    for (int i = 0; i < aCount; ++i) {
        bool found = false;
        for (int j = 0; j < bCount; ++j) {
            if (a[i].wireId == b[j].wireId) {
                found = true;
                if (!qFuzzyCompare(a[i].value, b[j].value)) {
                    return true;
                }
                break;
            }
        }
        if (!found) {
            return true;
        }
    }

    for (int j = 0; j < bCount; ++j) {
        bool found = false;
        for (int i = 0; i < aCount; ++i) {
            if (b[j].wireId == a[i].wireId) {
                found = true;
                break;
            }
        }
        if (!found) {
            return true;
        }
    }

    return false;
}

#include "moc_inputbuffer.cpp"
