#pragma once

#include <QHash>
#include <QObject>
#include <QVector>

#include "control/controlproxy.h"
#include "netmix/controlallowlist.h"
#include "netmix/controlapplier.h"
#include "netmix/inputbuffer.h"
#include "netmix/prediction.h"
#include "netmix/protocol.h"

class RollbackEngine : public QObject {
    Q_OBJECT
  public:
    static constexpr int kDefaultWindow = 8;
    static constexpr int kMaxWindow = 30;
    static constexpr double kRampScale = 4.0;
    static constexpr int kMaxRampTicks = 4;

    explicit RollbackEngine(QObject* parent = nullptr);
    ~RollbackEngine() override;

    void setWindowSize(int ticks);
    int windowSize() const { return m_windowSize; }

    void setInputBuffer(InputBuffer* buffer) { m_pBuffer = buffer; }
    void setPredictionStrategy(PredictionStrategy* strategy) { m_pPrediction = strategy; }
    void setControlApplier(ControlApplier* applier) { m_pApplier = applier; }

    void initialize();
    void onTick(quint32 tick);

    int rollbackCount() const { return m_rollbackCount; }
    int windowExceededCount() const { return m_windowExceededCount; }

  signals:
    void rollbackPerformed(quint32 fromTick, quint32 toTick);
    void windowExceeded(quint32 tick);

  private:
    struct Snapshot {
        quint32 tick = 0;
        bool occupied = false;
        QVector<double> values;
    };

    int snapshotSlotForTick(quint32 tick) const;
    void takeSnapshot(quint32 tick);
    void restoreSnapshot(const Snapshot& snap);
    int rampTicksForCorrection(double target, double current) const;

    int m_windowSize = kDefaultWindow;
    QVector<Snapshot> m_snapshots;
    quint32 m_oldestSnapshotTick = 0;
    quint32 m_newestSnapshotTick = 0;
    bool m_hasSnapshots = false;

    InputBuffer* m_pBuffer = nullptr;
    PredictionStrategy* m_pPrediction = nullptr;
    ControlApplier* m_pApplier = nullptr;

    QVector<ControlProxy*> m_proxies;
    QVector<AllowlistEntry> m_entries;
    QHash<quint16, int> m_wireToEntryIdx;

    int m_rollbackCount = 0;
    int m_windowExceededCount = 0;
    bool m_loggedWindowExceeded = false;
    quint32 m_lastHandledDivTick = 0;
};
