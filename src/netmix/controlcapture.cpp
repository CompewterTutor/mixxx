#include "netmix/controlcapture.h"

#include "control/controlproxy.h"
#include "netmix/controlallowlist.h"
#include "netmix/sessionclock.h"
#include "util/logger.h"

namespace {
const mixxx::Logger kLogger("ControlCapture");
} // namespace

ControlCapture::ControlCapture(QObject* parent)
        : QObject(parent) {}

ControlCapture::~ControlCapture() {
    stop();
}

void ControlCapture::start(const SessionClock* clock) {
    if (m_pClock) {
        kLogger.warning() << "start() called while already started — stopping first";
        stop();
    }

    m_pClock = clock;
    const auto& entries = ControlAllowlist::entries();
    m_entries = entries; // copy for fast wireId lookup during callback

    m_proxies.reserve(entries.size());
    for (const auto& entry : entries) {
        auto* proxy = new ControlProxy(entry.key, this);
        m_proxies.append(proxy);
    }

    for (int i = 0; i < m_entries.size(); ++i) {
        const auto& entry = m_entries[i];
        ControlProxy* proxy = m_proxies[i];
        proxy->connectValueChanged(
                this,
                [this, wireId = entry.wireId](double value) {
                    onProxyValueChange(wireId, value);
                },
                Qt::DirectConnection);
    }

    kLogger.info() << "Capture started:" << m_proxies.size() << "controls";
}

void ControlCapture::stop() {
    if (!m_pClock) {
        return;
    }

    for (auto* proxy : m_proxies) {
        proxy->disconnect(this);
    }

    // Delete proxies (parented to this, Qt deletes them automatically)
    qDeleteAll(m_proxies);
    m_proxies.clear();
    m_entries.clear();
    m_pClock = nullptr;

    kLogger.info() << "Capture stopped";
}

void ControlCapture::onProxyValueChange(quint16 wireId, double value) {
    if (!m_pClock) {
        return;
    }
    const quint32 tick = m_pClock->agreedTick();
    emit captured(tick, wireId, value);
}

#include "moc_controlcapture.cpp"
