#pragma once

#include <QObject>
#include <QVector>

#include "netmix/protocol.h"

class InputFramePacker : public QObject {
    Q_OBJECT
  public:
    static constexpr int kMaxEventsPerTick = 32;
    static constexpr int kRingSize = 8;

    explicit InputFramePacker(QObject* parent = nullptr);
    ~InputFramePacker() override;

    void addEvent(quint16 wireId, double value);
    void finishTick(quint32 tick);
    QVector<NetmixInputFrame> framesForSend(int batchSize = 4) const;
    void clear();

    int currentEventCount() const { return m_currentCount; }

  private:
    struct TickFrame {
        bool finalized = false;
        quint32 tick = 0;
        NetmixInputFrameEvent events[kMaxEventsPerTick];
        int count = 0;
    };

    TickFrame& currentFrame();
    const TickFrame& readFrame(int index) const;
    int advanceIndex(int index) const;

    TickFrame m_ring[kRingSize];
    int m_head = 0;        // current (in-progress) slot
    quint32 m_currentTick = 0;
    int m_currentCount = 0;
};
