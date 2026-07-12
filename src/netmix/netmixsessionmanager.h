#pragma once

#include <QObject>

class NetmixSessionManager : public QObject {
    Q_OBJECT
  public:
    NetmixSessionManager(QObject* parent = nullptr);
    ~NetmixSessionManager() override;
};
