#include "netmix/quantizer.h"

#include <QtMath>

NetmixQuantizer::NetmixQuantizer(QObject* parent)
        : QObject(parent), m_enabled(false) {
}

NetmixQuantizer::~NetmixQuantizer() = default;

void NetmixQuantizer::setEnabled(bool enabled) {
    m_enabled = enabled;
}

quint32 NetmixQuantizer::snap(quint32 tick, double bpm, int tickRate) const {
    if (!m_enabled || bpm <= 0.0) {
        return tick;
    }
    double tpg = static_cast<double>(tickRate) * 60.0 / (bpm * 16.0);
    return static_cast<quint32>(qRound(static_cast<double>(tick) / tpg) * tpg + 0.5);
}

#include "moc_quantizer.cpp"
