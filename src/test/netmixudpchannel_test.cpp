#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDataStream>
#include <QElapsedTimer>
#include <QIODevice>

#include "netmix/udpchannel.h"
#include "test/mixxxtest.h"

namespace {

class NetmixUdpChannelTest : public MixxxTest {
  protected:
    static void pumpEvents(int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(
                    QEventLoop::AllEvents, 10);
        }
    }

    static QByteArray craftDatagram(
            quint32 seq, const NetmixInputFrame& frame) {
        NetmixMessage msg;
        msg.type = NetmixMessageType::InputFrame;
        msg.payload = frame;

        QByteArray encoded = encodeMessage(msg);

        QByteArray datagram;
        QDataStream stream(&datagram, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.setVersion(QDataStream::Qt_6_0);
        stream << seq;
        datagram.append(encoded);
        return datagram;
    }

    static bool createPair(
            std::unique_ptr<UdpChannel>& a,
            std::unique_ptr<UdpChannel>& b,
            int timeoutMs = 1000) {
        a = std::make_unique<UdpChannel>();
        b = std::make_unique<UdpChannel>();

        if (!a->bind(0))
            return false;
        if (!b->bind(0))
            return false;

        a->setPeer(QHostAddress::LocalHost,
                b->socket()->localPort());
        b->setPeer(QHostAddress::LocalHost,
                a->socket()->localPort());

        pumpEvents(timeoutMs);
        return true;
    }
};

TEST_F(NetmixUdpChannelTest, LoopbackExchange) {
    std::unique_ptr<UdpChannel> a;
    std::unique_ptr<UdpChannel> b;
    ASSERT_TRUE(createPair(a, b));

    int receiveCount = 0;
    b->connect(b.get(), &UdpChannel::inputFrameReceived,
            [&](quint32, const QVector<NetmixInputFrameEvent>&) {
                receiveCount++;
            });

    QVector<NetmixInputFrame> frames;
    {
        NetmixInputFrame f;
        f.baseTick = 100;
        f.events = {{1, 0.5}, {2, 0.8}};
        frames.append(f);
    }
    {
        NetmixInputFrame f;
        f.baseTick = 101;
        f.events = {{1, 0.6}};
        frames.append(f);
    }
    {
        NetmixInputFrame f;
        f.baseTick = 102;
        f.events = {{3, 1.0}};
        frames.append(f);
    }

    a->sendFrames(frames);
    pumpEvents(500);

    EXPECT_EQ(3, receiveCount);
    EXPECT_EQ(3u, a->sentCount());
    EXPECT_EQ(3u, b->receivedCount());
    EXPECT_EQ(0u, b->droppedCount());
    EXPECT_EQ(0u, b->outOfOrderCount());

    a->disconnectFromPeer();
    b->disconnectFromPeer();
}

TEST_F(NetmixUdpChannelTest, DuplicateDrop) {
    std::unique_ptr<UdpChannel> a;
    std::unique_ptr<UdpChannel> b;
    ASSERT_TRUE(createPair(a, b));

    int receiveCount = 0;
    b->connect(b.get(), &UdpChannel::inputFrameReceived,
            [&](quint32, const QVector<NetmixInputFrameEvent>&) {
                receiveCount++;
            });

    NetmixInputFrame frame;
    frame.baseTick = 42;
    frame.events = {{1, 0.5}};

    // Send one frame from A (seq=0)
    QVector<NetmixInputFrame> frames;
    frames.append(frame);
    a->sendFrames(frames);
    pumpEvents(200);

    EXPECT_EQ(1, receiveCount);

    // Craft duplicate datagram with same seq=0
    QUdpSocket rawSender;
    rawSender.bind(0);
    QByteArray dupDg = craftDatagram(0, frame);
    rawSender.writeDatagram(dupDg, QHostAddress::LocalHost,
            b->socket()->localPort());
    pumpEvents(200);

    EXPECT_EQ(1, receiveCount);
    EXPECT_EQ(1u, b->receivedCount());
    EXPECT_EQ(1u, b->droppedCount());
    EXPECT_EQ(0u, b->outOfOrderCount());

    a->disconnectFromPeer();
    b->disconnectFromPeer();
}

TEST_F(NetmixUdpChannelTest, ReorderTolerance) {
    std::unique_ptr<UdpChannel> a;
    std::unique_ptr<UdpChannel> b;
    ASSERT_TRUE(createPair(a, b));

    int receiveCount = 0;
    b->connect(b.get(), &UdpChannel::inputFrameReceived,
            [&](quint32, const QVector<NetmixInputFrameEvent>&) {
                receiveCount++;
            });

    QUdpSocket rawSender;
    rawSender.bind(0);
    quint16 bPort = b->socket()->localPort();

    NetmixInputFrame f1;
    f1.baseTick = 100;
    f1.events = {{1, 0.1}};
    NetmixInputFrame f2;
    f2.baseTick = 101;
    f2.events = {{2, 0.2}};
    NetmixInputFrame f3;
    f3.baseTick = 102;
    f3.events = {{3, 0.3}};
    NetmixInputFrame f4;
    f4.baseTick = 103;
    f4.events = {{4, 0.4}};

    // Send seqs: 0, 1, 3, 2 (last two reordered)
    rawSender.writeDatagram(craftDatagram(0, f1),
            QHostAddress::LocalHost, bPort);
    pumpEvents(50);
    rawSender.writeDatagram(craftDatagram(1, f2),
            QHostAddress::LocalHost, bPort);
    pumpEvents(50);
    rawSender.writeDatagram(craftDatagram(3, f4),
            QHostAddress::LocalHost, bPort);
    pumpEvents(50);
    rawSender.writeDatagram(craftDatagram(2, f3),
            QHostAddress::LocalHost, bPort);
    pumpEvents(300);

    EXPECT_EQ(4, receiveCount);
    EXPECT_EQ(4u, b->receivedCount());
    EXPECT_EQ(0u, b->droppedCount());
    EXPECT_EQ(1u, b->outOfOrderCount());

    a->disconnectFromPeer();
    b->disconnectFromPeer();
}

TEST_F(NetmixUdpChannelTest, StaleDrop) {
    std::unique_ptr<UdpChannel> a;
    std::unique_ptr<UdpChannel> b;
    ASSERT_TRUE(createPair(a, b));

    int receiveCount = 0;
    b->connect(b.get(), &UdpChannel::inputFrameReceived,
            [&](quint32, const QVector<NetmixInputFrameEvent>&) {
                receiveCount++;
            });

    QUdpSocket rawSender;
    rawSender.bind(0);
    quint16 bPort = b->socket()->localPort();

    NetmixInputFrame dummy;
    dummy.baseTick = 0;

    // Send seqs 0..99 to advance receiver window
    for (quint32 seq = 0; seq < 100; ++seq) {
        rawSender.writeDatagram(craftDatagram(seq, dummy),
                QHostAddress::LocalHost, bPort);
        pumpEvents(5);
    }
    pumpEvents(300);

    EXPECT_EQ(100, receiveCount);
    EXPECT_EQ(100u, b->receivedCount());
    EXPECT_EQ(0u, b->droppedCount());

    // Send stale seq=1 (diff=99-1=98 > 64)
    rawSender.writeDatagram(craftDatagram(1, dummy),
            QHostAddress::LocalHost, bPort);
    pumpEvents(200);

    EXPECT_EQ(100, receiveCount);
    EXPECT_EQ(100u, b->receivedCount());
    EXPECT_EQ(1u, b->droppedCount());

    a->disconnectFromPeer();
    b->disconnectFromPeer();
}

TEST_F(NetmixUdpChannelTest, DecodeErrorDrop) {
    std::unique_ptr<UdpChannel> a;
    std::unique_ptr<UdpChannel> b;
    ASSERT_TRUE(createPair(a, b));

    int receiveCount = 0;
    b->connect(b.get(), &UdpChannel::inputFrameReceived,
            [&](quint32, const QVector<NetmixInputFrameEvent>&) {
                receiveCount++;
            });

    QUdpSocket rawSender;
    rawSender.bind(0);

    // Craft datagram with valid seq but garbage payload
    QByteArray badDg;
    {
        QDataStream stream(&badDg, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.setVersion(QDataStream::Qt_6_0);
        stream << quint32(0);
    }
    badDg.append(QByteArray("not a valid netmix message", 25));

    rawSender.writeDatagram(badDg, QHostAddress::LocalHost,
            b->socket()->localPort());
    pumpEvents(200);

    EXPECT_EQ(0, receiveCount);
    EXPECT_EQ(0u, b->receivedCount());
    EXPECT_EQ(1u, b->droppedCount());
    EXPECT_EQ(0u, b->outOfOrderCount());

    a->disconnectFromPeer();
    b->disconnectFromPeer();
}

TEST_F(NetmixUdpChannelTest, StatsResetOnDisconnect) {
    std::unique_ptr<UdpChannel> a;
    std::unique_ptr<UdpChannel> b;
    ASSERT_TRUE(createPair(a, b));

    int receiveCount = 0;
    b->connect(b.get(), &UdpChannel::inputFrameReceived,
            [&](quint32, const QVector<NetmixInputFrameEvent>&) {
                receiveCount++;
            });

    // Send some frames
    NetmixInputFrame frame;
    frame.baseTick = 0;
    frame.events = {{1, 0.5}};
    QVector<NetmixInputFrame> frames;
    frames.append(frame);
    a->sendFrames(frames);
    pumpEvents(200);

    EXPECT_EQ(1, receiveCount);
    EXPECT_EQ(1u, a->sentCount());
    EXPECT_EQ(1u, b->receivedCount());

    a->disconnectFromPeer();
    b->disconnectFromPeer();

    EXPECT_EQ(0u, a->sentCount());
    EXPECT_EQ(0u, a->receivedCount());
    EXPECT_EQ(0u, a->droppedCount());
    EXPECT_EQ(0u, a->outOfOrderCount());
    EXPECT_EQ(0u, b->sentCount());
    EXPECT_EQ(0u, b->receivedCount());
    EXPECT_EQ(0u, b->droppedCount());
    EXPECT_EQ(0u, b->outOfOrderCount());
}

} // namespace
