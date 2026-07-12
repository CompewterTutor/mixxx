#include "netmix/inputframe.h"

#include <QtGlobal>

InputFramePacker::InputFramePacker(QObject* parent)
        : QObject(parent) {
    clear();
}

InputFramePacker::~InputFramePacker() = default;

void InputFramePacker::clear() {
    for (int i = 0; i < kRingSize; ++i) {
        m_ring[i].finalized = false;
        m_ring[i].tick = 0;
        m_ring[i].count = 0;
    }
    m_head = 0;
    m_currentTick = 0;
    m_currentCount = 0;
}

InputFramePacker::TickFrame& InputFramePacker::currentFrame() {
    return m_ring[m_head];
}

const InputFramePacker::TickFrame& InputFramePacker::readFrame(int index) const {
    return m_ring[index];
}

int InputFramePacker::advanceIndex(int index) const {
    return (index + 1) % kRingSize;
}

void InputFramePacker::addEvent(quint16 wireId, double value) {
    TickFrame& frame = currentFrame();

    // Dedup: scan for existing wireId and overwrite (last value wins)
    for (int i = 0; i < frame.count; ++i) {
        if (frame.events[i].wireId == wireId) {
            frame.events[i].value = value;
            return;
        }
    }

    // Not found: append if there's room
    if (frame.count < kMaxEventsPerTick) {
        frame.events[frame.count] = {wireId, value};
        ++frame.count;
        ++m_currentCount;
    }
}

void InputFramePacker::finishTick(quint32 tick) {
    TickFrame& frame = currentFrame();
    frame.tick = tick;
    frame.finalized = true;

    // Advance to next slot
    m_head = advanceIndex(m_head);
    m_currentTick = tick;
    m_currentCount = 0;

    // Reset new current slot
    TickFrame& next = currentFrame();
    next.finalized = false;
    next.tick = 0;
    next.count = 0;
}

QVector<NetmixInputFrame> InputFramePacker::framesForSend(int batchSize) const {
    QVector<NetmixInputFrame> result;
    result.reserve(batchSize);

    // Walk backwards from the most recently finalized frame
    int idx = m_head;
    int collected = 0;

    for (int step = 1; step <= kRingSize && collected < batchSize; ++step) {
        int scanIdx = (idx - step + kRingSize) % kRingSize;
        const TickFrame& frame = readFrame(scanIdx);
        if (!frame.finalized) {
            continue;
        }

        NetmixInputFrame outFrame;
        outFrame.baseTick = frame.tick;
        outFrame.events.reserve(frame.count);
        for (int i = 0; i < frame.count; ++i) {
            outFrame.events.append(frame.events[i]);
        }
        result.append(outFrame);
        ++collected;
    }

    return result;
}

#include "moc_inputframe.cpp"
