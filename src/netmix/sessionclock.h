#pragma once

#include <QObject>
#include <QtGlobal>

class SessionClock : public QObject {
    Q_OBJECT
  public:
    static constexpr int kTickRate = 240;

    explicit SessionClock(QObject* parent = nullptr);
    ~SessionClock() override;

    void onFramesProcessed(int frames, int sampleRate);
    quint32 currentTick() const { return m_currentTick; }
    quint32 agreedTick() const;
    void setOffset(qint32 offset) { m_offset = offset; }
    qint32 offset() const { return m_offset; }
    void reset();

  signals:
    void tickAdvanced(quint32 tick);

  private:
    quint64 m_totalFrames = 0;
    quint32 m_currentTick = 0;
    qint32 m_offset = 0;
};
