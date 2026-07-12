#include "netmix/clocksync.h"

#include <QDataStream>
#include <QIODevice>

#include <algorithm>
#include <vector>

#include "util/logger.h"

namespace {

mixxx::Logger kLog("Netmix ClockSync");

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

ClockSync::ClockSync(SessionClock* pClock, QObject* parent)
        : QObject(parent),
          m_pClock(pClock) {
    m_elapsed.start();
}

ClockSync::~ClockSync() {
    stop();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ClockSync::start(quint16 localPort, QHostAddress peerAddr, quint16 peerPort) {
    if (m_running) {
        qWarning("[Netmix] ClockSync::start: already running");
        return;
    }

    m_peerAddress = peerAddr;
    m_peerPort = peerPort;
    m_testMode = false;

    m_pSocket = new QUdpSocket(this);
    if (!m_pSocket->bind(QHostAddress::Any, localPort)) {
        qWarning("[Netmix] ClockSync::start: bind failed on port %u: %s",
                localPort, qPrintable(m_pSocket->errorString()));
        delete m_pSocket;
        m_pSocket = nullptr;
        return;
    }

    connect(m_pSocket, &QUdpSocket::readyRead,
            this, &ClockSync::onReadyRead);

    m_elapsed.start();
    m_pPingTimer = new QTimer(this);
    connect(m_pPingTimer, &QTimer::timeout,
            this, &ClockSync::onSendPing);
    m_pPingTimer->start(kPingIntervalMs);

    m_running = true;
    kLog.debug() << "ClockSync started on port" << localPort;
}

void ClockSync::startTestMode() {
    m_testMode = true;
    m_elapsed.start();
    m_running = true;
    kLog.debug() << "ClockSync started in test mode";
}

void ClockSync::stop() {
    if (m_pPingTimer) {
        m_pPingTimer->stop();
    }

    if (m_pSocket) {
        m_pSocket->close();
        m_pSocket->deleteLater();
        m_pSocket = nullptr;
    }

    m_running = false;
    m_pendingPong = false;
    m_sampleCount = 0;
    m_nextSlot = 0;
    m_currentOffset = 0;
    m_pongCount = 0;
    m_smoothedRttMs = 0.0;
    m_seqNumber = 0;
}

void ClockSync::setInitialOffset(quint32 hostTick, quint32 localTick) {
    qint32 offset = static_cast<qint32>(hostTick) - static_cast<qint32>(localTick);
    m_pClock->setOffset(offset);
    kLog.debug() << "Initial offset set:" << offset;
}

// ---------------------------------------------------------------------------
// Ping / Pong
// ---------------------------------------------------------------------------

void ClockSync::sendPingNow() {
    if (m_pendingPong) {
        return;
    }

    m_lastSentTick = m_pClock->agreedTick();
    m_lastSentTimeUsec = m_elapsed.nsecsElapsed() / 1000;
    m_pendingPong = true;

    NetmixPing ping;
    ping.sentTick = m_lastSentTick;

    NetmixMessage msg;
    msg.type = NetmixMessageType::Ping;
    msg.payload = ping;

    sendMessage(msg);
}

void ClockSync::onSendPing() {
    sendPingNow();
}

void ClockSync::onReadyRead() {
    while (m_pSocket && m_pSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(m_pSocket->pendingDatagramSize()));
        m_pSocket->readDatagram(datagram.data(), datagram.size());

        if (datagram.size() < 4) {
            continue;
        }

        QByteArray payload = datagram.mid(4);
        auto decoded = decodeMessage(payload);
        if (!decoded.has_value()) {
            continue;
        }

        injectMessage(*decoded);
    }
}

void ClockSync::injectMessage(const NetmixMessage& msg) {
    switch (msg.type) {
    case NetmixMessageType::Ping:
        handlePing(std::get<NetmixPing>(msg.payload));
        break;
    case NetmixMessageType::Pong:
        handlePong(std::get<NetmixPong>(msg.payload));
        break;
    default:
        break;
    }
}

void ClockSync::handlePing(const NetmixPing& ping) {
    NetmixPong pong;
    pong.sentTick = ping.sentTick;
    pong.remoteTick = m_pClock->agreedTick();

    NetmixMessage msg;
    msg.type = NetmixMessageType::Pong;
    msg.payload = pong;

    sendMessage(msg);
}

void ClockSync::handlePong(const NetmixPong& pong) {
    if (pong.sentTick != m_lastSentTick) {
        // Not our pong — ignore (stale or from another exchange)
        return;
    }

    quint64 nowUsec = m_elapsed.nsecsElapsed() / 1000;
    double rttMs = static_cast<double>(nowUsec - m_lastSentTimeUsec) / 1000.0;
    m_smoothedRttMs = rttMs;
    emit rttUpdated(rttMs);

    quint32 localTickNow = m_pClock->agreedTick();
    qint32 offsetEstimate = static_cast<qint32>(pong.remoteTick) -
            (static_cast<qint32>(m_lastSentTick) +
                    static_cast<qint32>(localTickNow)) /
                    2;

    updateFilter(offsetEstimate);
    m_pendingPong = false;
}

// ---------------------------------------------------------------------------
// Median filter
// ---------------------------------------------------------------------------

void ClockSync::updateFilter(qint32 sample) {
    m_offsetSamples[m_nextSlot] = sample;
    m_nextSlot = (m_nextSlot + 1) % kMedianWindowSize;
    if (m_sampleCount < kMedianWindowSize) {
        m_sampleCount++;
    }

    m_currentOffset = computeMedian();

    m_pongCount++;
    if (m_pongCount % kUpdateEveryNPongs == 0) {
        qint32 diff = m_currentOffset - m_pClock->offset();
        if (diff > 1 || diff < -1) {
            m_pClock->setOffset(m_currentOffset);
            emit offsetUpdated(m_currentOffset);
        }
    }
}

qint32 ClockSync::computeMedian() const {
    if (m_sampleCount == 0) {
        return 0;
    }

    std::vector<qint32> sorted;
    sorted.reserve(m_sampleCount);

    if (m_sampleCount == kMedianWindowSize) {
        for (int i = m_nextSlot; i < kMedianWindowSize; i++) {
            sorted.push_back(m_offsetSamples[i]);
        }
        for (int i = 0; i < m_nextSlot; i++) {
            sorted.push_back(m_offsetSamples[i]);
        }
    } else {
        for (int i = 0; i < m_sampleCount; i++) {
            sorted.push_back(m_offsetSamples[i]);
        }
    }

    std::sort(sorted.begin(), sorted.end());
    return sorted[m_sampleCount / 2];
}

// ---------------------------------------------------------------------------
// Message send
// ---------------------------------------------------------------------------

void ClockSync::sendMessage(const NetmixMessage& msg) {
    emit outgoingMessage(msg);

    if (m_testMode || !m_pSocket) {
        return;
    }

    QByteArray encoded = encodeMessage(msg);
    QByteArray datagram;
    {
        QDataStream stream(&datagram, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.setVersion(QDataStream::Qt_6_0);
        stream << m_seqNumber;
    }
    m_seqNumber++;
    datagram.append(encoded);

    m_pSocket->writeDatagram(datagram, m_peerAddress, m_peerPort);
}

#include "moc_clocksync.cpp"
