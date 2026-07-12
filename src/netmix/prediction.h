#pragma once

#include <memory>

#include "netmix/inputbuffer.h"
#include "netmix/protocol.h"

class PredictionStrategy {
  public:
    virtual ~PredictionStrategy() = default;
    virtual NetmixInputFrame predict(quint32 tick, const InputBuffer& buffer) = 0;
};

class HoldLastPrediction : public PredictionStrategy {
  public:
    NetmixInputFrame predict(quint32 tick, const InputBuffer& buffer) override;
};
