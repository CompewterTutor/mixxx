#pragma once

#include <QAtomicInt>
#include <QObject>
#include <QTimer>
#include <QVector>
#include <atomic>

#include "control/controlproxy.h"
#include "netmix/controlallowlist.h"
#include "util/fifo.h"

class ChannelOwnership;
class SessionClock;

// One captured control event. Must stay trivially copyable: instances travel
// through a lock-free FIFO written from arbitrary threads (including the
// engine thread via ControlProxy DirectConnection callbacks).
struct CapturedControlEvent {
    quint32 tick;
    quint16 wireId;
    double value;
};

class ControlCapture : public QObject {
    Q_OBJECT
  public:
    explicit ControlCapture(QObject* parent = nullptr);
    ~ControlCapture() override;

    void start(const SessionClock* clock);
    void stop();

    // Thread-safe: may be called while capture callbacks are running on any
    // thread. Muting is enforced at capture time so echo suppression windows
    // around remote applies hold regardless of drain latency.
    void setMuted(bool muted) {
        m_muted.store(muted, std::memory_order_relaxed);
    }
    bool isMuted() const {
        return m_muted.load(std::memory_order_relaxed);
    }

    void setOwnership(ChannelOwnership* ownership) { m_pOwnership = ownership; }
    const QVector<ControlProxy*>& proxies() const { return m_proxies; }

  signals:
    void captured(quint32 tick, quint16 wireId, double value);

  private:
    void onProxyValueChange(quint16 wireId, double value);
    void drainCaptured();

    std::atomic<bool> m_muted{false};
    std::atomic<int> m_droppedEvents{0};
    // Serializes FIFO writes: control events can originate from several
    // threads (engine thread for playposition, GUI thread for knobs) and the
    // PortAudio ring buffer underneath FIFO is single-producer.
    QAtomicInt m_producerLock;
    const SessionClock* m_pClock = nullptr;
    ChannelOwnership* m_pOwnership = nullptr;
    QVector<ControlProxy*> m_proxies;
    QVector<AllowlistEntry> m_entries;
    FIFO<CapturedControlEvent> m_captureFifo{4096};
    QTimer* m_pDrainTimer = nullptr;
};
