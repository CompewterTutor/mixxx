#include "netmix/sessionclock.h"

SessionClock::SessionClock(QObject* parent)
        : QObject(parent) {}

SessionClock::~SessionClock() = default;

void SessionClock::onFramesProcessed(int frames, int sampleRate) {
    if (frames <= 0 || sampleRate <= 0) {
        return;
    }

    m_totalFrames += static_cast<quint64>(frames);

    // Compute tick via integer-only rational arithmetic: floor(totalFrames * tickRate / sampleRate)
    // This avoids float error accumulation entirely — every call recomputes from totalFrames.
    quint32 newTick = static_cast<quint32>(
            (m_totalFrames * static_cast<quint64>(kTickRate)) / static_cast<quint64>(sampleRate));

    if (newTick > m_currentTick) {
        m_currentTick = newTick;
        emit tickAdvanced(m_currentTick);
    }
}

quint32 SessionClock::agreedTick() const {
    // Wraparound-safe addition via 64-bit intermediate then truncate to 32 bits.
    // Two's complement subtraction for tick deltas works naturally at 32 bits,
    // so peers can compute signed differences from agreedTick.
    return static_cast<quint32>(static_cast<qint64>(m_currentTick) + m_offset);
}

void SessionClock::reset() {
    m_totalFrames = 0;
    m_currentTick = 0;
    m_offset = 0;
}

#include "moc_sessionclock.cpp"
