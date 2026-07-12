#pragma once

#include <QObject>
#include <QVector>
#include <optional>

#include "netmix/protocol.h"

class InputBuffer : public QObject {
    Q_OBJECT
  public:
    static constexpr int kDefaultCapacity = 256;
    static constexpr int kMaxEventsPerTick = 32;

    explicit InputBuffer(QObject* parent = nullptr);
    ~InputBuffer() override;

    void setCapacity(int ticks);
    int capacity() const { return m_capacity; }

    void insertRemoteConfirmed(quint32 tick, const NetmixInputFrame& frame);
    void insertRemotePredicted(quint32 tick, const NetmixInputFrame& frame);
    void insertLocal(quint32 tick, const NetmixInputFrame& frame);

    std::optional<quint32> firstDivergentTick() const;

    bool hasRemote(quint32 tick) const;
    bool isRemoteConfirmed(quint32 tick) const;
    bool hasLocal(quint32 tick) const;

    NetmixInputFrame remoteFrameAt(quint32 tick) const;
    NetmixInputFrame localFrameAt(quint32 tick) const;

    void clear();
    void advanceWindow(quint32 newEarliestTick);

  private:
    struct RemoteTickSlot {
        quint32 tick = 0;
        bool occupied = false;
        bool confirmed = false;
        bool predicted = false;
        bool divergent = false;
        NetmixInputFrameEvent events[kMaxEventsPerTick];
        int eventCount = 0;
    };

    struct LocalTickSlot {
        quint32 tick = 0;
        bool occupied = false;
        NetmixInputFrameEvent events[kMaxEventsPerTick];
        int eventCount = 0;
    };

    int slotForTick(quint32 tick) const;

    void updateWindow(quint32 tick);
    void evictStaleSlots();

    static bool eventsDiffer(
            const NetmixInputFrameEvent* a, int aCount,
            const NetmixInputFrameEvent* b, int bCount);

    int m_capacity = kDefaultCapacity;
    quint32 m_oldestTick = 0;
    quint32 m_newestTick = 0;
    bool m_hasEntries = false;
    mutable std::optional<quint32> m_cachedFirstDivergentTick;

    QVector<RemoteTickSlot> m_remoteRing;
    QVector<LocalTickSlot> m_localRing;
};
