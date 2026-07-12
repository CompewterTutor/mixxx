#pragma once

#include <QObject>
#include <QtGlobal>

class NetmixQuantizer : public QObject {
    Q_OBJECT
  public:
    explicit NetmixQuantizer(QObject* parent = nullptr);
    ~NetmixQuantizer() override;

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    quint32 snap(quint32 tick, double bpm, int tickRate) const;

  private:
    bool m_enabled = false;
};
