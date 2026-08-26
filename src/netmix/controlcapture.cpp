#include "netmix/controlcapture.h"

#include <QTimer>

#include "control/controlproxy.h"
#include "netmix/channelownership.h"
#include "netmix/controlallowlist.h"
#include "netmix/sessionclock.h"
#include "util/logger.h"

namespace {
const mixxx::Logger kLogger("ControlCapture");

// GUI-thread drain interval. At the 240 Hz session tick this keeps drain
// latency well under one tick while batching events between event-loop passes.
constexpr int kDrainIntervalMs = 8;

// FIFO capacity in events. Allowlist is on the order of a hundred controls;
// even a full gesture storm across all of them stays far below this.
constexpr int kCaptureFifoSize = 4096;
} // namespace

ControlCapture::ControlCapture(QObject* parent)
        : QObject(parent), m_captureFifo(kCaptureFifoSize) {
}

ControlCapture::~ControlCapture() {
    stop();
}

void ControlCapture::start(const SessionClock* clock) {
    if (m_pClock) {
        kLogger.warning() << "start() called while already started — stopping first";
        stop();
    }

    m_pClock = clock;
    const auto& entries = ControlAllowlist::entries();
    m_entries = entries; // copy for fast wireId lookup during callback

    m_proxies.reserve(entries.size());
    for (const auto& entry : entries) {
        auto* proxy = new ControlProxy(entry.key, this);
        m_proxies.append(proxy);
    }

    for (int i = 0; i < m_entries.size(); ++i) {
        const auto& entry = m_entries[i];
        ControlProxy* proxy = m_proxies[i];
        // DirectConnection: the lambda only stamps a tick and pushes into the
        // lock-free FIFO, so it is safe to run on whichever thread sets the
        // ControlObject (including the engine thread). All non-realtime-safe
        // work happens in drainCaptured() on the GUI thread.
        proxy->connectValueChanged(
                this,
                [this, wireId = entry.wireId](double value) {
                    onProxyValueChange(wireId, value);
                },
                Qt::DirectConnection);
    }

    if (!m_pDrainTimer) {
        m_pDrainTimer = new QTimer(this);
        m_pDrainTimer->setInterval(kDrainIntervalMs);
        connect(m_pDrainTimer, &QTimer::timeout, this, &ControlCapture::drainCaptured);
    }
    m_pDrainTimer->start();

    kLogger.info() << "Capture started:" << m_proxies.size() << "controls";
}

void ControlCapture::stop() {
    if (m_pDrainTimer) {
        m_pDrainTimer->stop();
    }

    if (!m_pClock) {
        return;
    }

    for (auto* proxy : m_proxies) {
        proxy->disconnect(this);
    }

    // Drop any captured-but-undelivered events; the session is ending.
    m_captureFifo.flushReadData(m_captureFifo.readAvailable());
    m_droppedEvents.store(0, std::memory_order_relaxed);

    qDeleteAll(m_proxies);
    m_proxies.clear();
    m_entries.clear();
    m_pClock = nullptr;

    kLogger.info() << "Capture stopped";
}

void ControlCapture::onProxyValueChange(quint16 wireId, double value) {
    if (m_muted.load(std::memory_order_relaxed) || !m_pClock) {
        return;
    }

    CapturedControlEvent event{m_pClock->agreedTick(), wireId, value};

    // Bounded spin ticket: never blocks on a held OS lock, only waits out the
    // few nanoseconds of another producer's FIFO write. No allocation, no
    // syscall — realtime-safe.
    while (!m_producerLock.testAndSetAcquire(0, 1)) {
    }
    const bool written = m_captureFifo.write(&event, 1) == 1;
    m_producerLock.storeRelease(0);

    if (!written) {
        m_droppedEvents.fetch_add(1, std::memory_order_relaxed);
    }
}

void ControlCapture::drainCaptured() {
    const int dropped = m_droppedEvents.exchange(0, std::memory_order_relaxed);
    if (dropped > 0) {
        kLogger.warning() << "Capture FIFO overflow:" << dropped
                          << "control events dropped";
    }

    CapturedControlEvent event;
    while (m_captureFifo.read(&event, 1) == 1) {
        // Ownership check at drain time: ChannelOwnership state is owned by
        // the session manager (GUI thread), so it must not be touched from
        // capture callbacks running on arbitrary threads.
        if (m_pOwnership) {
            auto channelId = ControlAllowlist::channelForWireId(event.wireId);
            if (channelId.has_value() && !m_pOwnership->isOwnedByLocal(*channelId)) {
                continue;
            }
        }
        emit captured(event.tick, event.wireId, event.value);
    }
}

#include "moc_controlcapture.cpp"
