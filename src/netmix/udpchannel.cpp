#include "netmix/udpchannel.h"

#include <QDataStream>
#include <QIODevice>

UdpChannel::UdpChannel(QObject* parent)
        : QObject(parent) {
}

UdpChannel::~UdpChannel() {
    disconnectFromPeer();
}

void UdpChannel::setPeer(const QHostAddress& address, quint16 port) {
    m_peerAddress = address;
    m_peerPort = port;
}

bool UdpChannel::bind(quint16 port) {
    if (m_pSocket) {
        qWarning("[Netmix] UdpChannel::bind: already bound");
        return false;
    }

    m_pSocket = new QUdpSocket(this);
    if (!m_pSocket->bind(QHostAddress::Any, port,
                QAbstractSocket::ShareAddress | QAbstractSocket::ReuseAddressHint)) {
        qWarning("[Netmix] UdpChannel::bind failed on port %u: %s",
                port, qPrintable(m_pSocket->errorString()));
        delete m_pSocket;
        m_pSocket = nullptr;
        return false;
    }

    connect(m_pSocket, &QUdpSocket::readyRead,
            this, &UdpChannel::onReadyRead);
    return true;
}

void UdpChannel::sendFrames(const QVector<NetmixInputFrame>& frames) {
    if (!m_pSocket) {
        qWarning("[Netmix] UdpChannel::sendFrames: not bound");
        return;
    }

    for (const auto& frame : frames) {
        NetmixMessage msg;
        msg.type = NetmixMessageType::InputFrame;
        msg.payload = frame;

        QByteArray encoded = encodeMessage(msg);

        // Prepend sequence number as little-endian quint32
        QByteArray datagram;
        {
            QDataStream stream(&datagram, QIODevice::WriteOnly);
            stream.setByteOrder(QDataStream::LittleEndian);
            stream.setVersion(QDataStream::Qt_6_0);
            stream << m_sequenceNumber;
        }
        datagram.append(encoded);

        qint64 sent = m_pSocket->writeDatagram(
                datagram, m_peerAddress, m_peerPort);
        if (sent < 0) {
            qWarning("[Netmix] UdpChannel::sendFrames: "
                     "writeDatagram failed: %s",
                    qPrintable(m_pSocket->errorString()));
        } else {
            m_sentCount++;
        }

        m_sequenceNumber++;
    }
}

void UdpChannel::disconnectFromPeer() {
    if (m_pSocket) {
        m_pSocket->close();
        m_pSocket->deleteLater();
        m_pSocket = nullptr;
    }
    m_sentCount = 0;
    m_receivedCount = 0;
    m_droppedCount = 0;
    m_outOfOrderCount = 0;
    m_highestReceived = 0;
    m_sequenceNumber = 0;
    m_receivedSeqs.clear();
}

bool UdpChannel::isStale(quint32 seq) const {
    if (m_receivedSeqs.isEmpty()) {
        return false;
    }
    qint32 diff = static_cast<qint32>(m_highestReceived - seq);
    return diff >= static_cast<qint32>(kWindowSize);
}

void UdpChannel::pruneReceivedSeqs() {
    QSet<quint32> pruned;
    for (quint32 seq : m_receivedSeqs) {
        qint32 diff = static_cast<qint32>(m_highestReceived - seq);
        if (diff < static_cast<qint32>(kWindowSize) && diff >= 0) {
            pruned.insert(seq);
        }
    }
    m_receivedSeqs = pruned;
}

void UdpChannel::onReadyRead() {
    while (m_pSocket && m_pSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_pSocket->pendingDatagramSize());
        m_pSocket->readDatagram(datagram.data(), datagram.size());

        if (datagram.size() < 4) {
            m_droppedCount++;
            continue;
        }

        // Extract sequence number (first 4 bytes, little-endian)
        quint32 seq;
        {
            QDataStream stream(datagram.left(4));
            stream.setByteOrder(QDataStream::LittleEndian);
            stream.setVersion(QDataStream::Qt_6_0);
            stream >> seq;
        }

        QByteArray payload = datagram.mid(4);

        // Stale check
        if (isStale(seq)) {
            m_droppedCount++;
            continue;
        }

        // Duplicate check
        if (m_receivedSeqs.contains(seq)) {
            m_droppedCount++;
            continue;
        }

        // Decode payload
        auto decoded = decodeMessage(payload);
        if (!decoded.has_value() ||
                decoded->type != NetmixMessageType::InputFrame) {
            m_droppedCount++;
            continue;
        }

        // Determine if seq is newer than current highest
        bool isNew = m_receivedSeqs.isEmpty() ||
                static_cast<qint32>(seq - m_highestReceived) > 0;

        if (!isNew && seq != m_highestReceived) {
            m_outOfOrderCount++;
        }

        // Accept
        const auto& frame =
                std::get<NetmixInputFrame>(decoded->payload);
        emit inputFrameReceived(frame.baseTick, frame.events);
        m_receivedCount++;

        m_receivedSeqs.insert(seq);
        if (isNew) {
            m_highestReceived = seq;
        }

        pruneReceivedSeqs();
    }
}

#include "moc_udpchannel.cpp"
