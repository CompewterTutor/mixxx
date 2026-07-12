#include <gtest/gtest.h>

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "netmix/tcpsession.h"
#include "netmix/trackcache.h"
#include "netmix/tracktransfer.h"
#include "test/mixxxtest.h"

namespace {

class NetmixTrackTransferTest : public MixxxTest {
  protected:
    void SetUp() override {
        m_hostCacheDir = std::make_unique<QTemporaryDir>();
        m_clientCacheDir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(m_hostCacheDir->isValid());
        ASSERT_TRUE(m_clientCacheDir->isValid());

        m_hostCache =
                std::make_unique<TrackCache>(m_hostCacheDir->path());
        m_clientCache =
                std::make_unique<TrackCache>(m_clientCacheDir->path());
        ASSERT_TRUE(m_hostCache->initialize());
        ASSERT_TRUE(m_clientCache->initialize());

        m_hostSession = std::make_unique<TcpSession>();
        m_hostSession->setDisplayName(QStringLiteral("host"));
        m_hostSession->setListenPort(0);
        ASSERT_TRUE(m_hostSession->listen());

        m_clientSession = std::make_unique<TcpSession>();
        m_clientSession->setDisplayName(QStringLiteral("client"));
        m_clientSession->setPeerAddress(
                QHostAddress::LocalHost,
                m_hostSession->server()->serverPort());
        m_clientSession->connectToPeer();
        pumpEvents(3000);

        ASSERT_EQ(TcpSession::Connected, m_hostSession->state());
        ASSERT_EQ(TcpSession::Connected, m_clientSession->state());

        m_hostTransfer = std::make_unique<TrackTransfer>(
                m_hostSession.get(), m_hostCache.get());
        m_clientTransfer = std::make_unique<TrackTransfer>(
                m_clientSession.get(), m_clientCache.get());
    }

    void TearDown() override {
        m_hostTransfer.reset();
        m_clientTransfer.reset();
        m_hostSession->disconnectFromPeer();
        m_clientSession->disconnectFromPeer();
        pumpEvents(500);
        m_hostSession.reset();
        m_clientSession.reset();
        m_hostCache.reset();
        m_clientCache.reset();
        m_hostCacheDir.reset();
        m_clientCacheDir.reset();
    }

    static void pumpEvents(int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
    }

    QString createTempFile(const QString& dirPath,
            const QString& name,
            qint64 sizeBytes) {
        QString path = QDir(dirPath).filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return QString();
        }
        QByteArray buf(65536, 0);
        for (qint64 offset = 0; offset < sizeBytes; offset += 65536) {
            qint64 chunkSize = qMin(static_cast<qint64>(65536), sizeBytes - offset);
            for (int i = 0; i < chunkSize; ++i) {
                buf[i] = static_cast<char>((offset + i) % 251) + 1;
            }
            if (file.write(buf.constData(), chunkSize) != chunkSize) {
                file.close();
                return QString();
            }
        }
        file.close();
        return path;
    }

    QString computeSha256Hex(const QString& filePath) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            return QString();
        }
        QCryptographicHash hasher(QCryptographicHash::Sha256);
        if (!hasher.addData(&file)) {
            return QString();
        }
        return QString::fromLatin1(hasher.result().toHex());
    }

    bool filesIdentical(const QString& pathA, const QString& pathB) {
        QFile fa(pathA), fb(pathB);
        if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly)) {
            return false;
        }
        if (fa.size() != fb.size()) {
            return false;
        }
        const qint64 kBufSize = 65536;
        QByteArray ba(kBufSize, 0), bb(kBufSize, 0);
        while (!fa.atEnd()) {
            qint64 ra = fa.read(ba.data(), kBufSize);
            qint64 rb = fb.read(bb.data(), kBufSize);
            if (ra != rb || memcmp(ba.data(), bb.data(), static_cast<size_t>(ra)) != 0) {
                return false;
            }
        }
        return true;
    }

    std::unique_ptr<QTemporaryDir> m_hostCacheDir;
    std::unique_ptr<QTemporaryDir> m_clientCacheDir;
    std::unique_ptr<TrackCache> m_hostCache;
    std::unique_ptr<TrackCache> m_clientCache;
    std::unique_ptr<TcpSession> m_hostSession;
    std::unique_ptr<TcpSession> m_clientSession;
    std::unique_ptr<TrackTransfer> m_hostTransfer;
    std::unique_ptr<TrackTransfer> m_clientTransfer;
};

// ---------------------------------------------------------------------------
// FullTransfer_Success
// ---------------------------------------------------------------------------

TEST_F(NetmixTrackTransferTest, FullTransfer_Success) {
    constexpr qint64 kFileSize = 5LL * 1024 * 1024;
    QString srcPath = createTempFile(
            m_hostCacheDir->path(), QStringLiteral("source.mp3"), kFileSize);
    ASSERT_FALSE(srcPath.isEmpty());
    ASSERT_TRUE(QFile::exists(srcPath));

    QString hash = computeSha256Hex(srcPath);
    ASSERT_EQ(64, hash.size());

    QSignalSpy hostCompleteSpy(m_hostTransfer.get(), &TrackTransfer::complete);
    QSignalSpy hostFailedSpy(m_hostTransfer.get(), &TrackTransfer::failed);
    QSignalSpy clientReceivedSpy(
            m_clientTransfer.get(), &TrackTransfer::trackReceived);

    m_hostTransfer->sendTrack(
            srcPath, hash, QStringLiteral("test.mp3"), QStringLiteral("audio/mpeg"));

    ASSERT_TRUE(hostCompleteSpy.wait(15000));

    EXPECT_EQ(0, hostFailedSpy.count());
    ASSERT_EQ(1, hostCompleteSpy.count());
    ASSERT_EQ(1, clientReceivedSpy.count());

    QString receivedHash = clientReceivedSpy.at(0).at(0).toString();
    EXPECT_EQ(hash, receivedHash);

    EXPECT_TRUE(m_clientCache->contains(hash));
    EXPECT_TRUE(m_clientCache->verify(hash));

    QString cachedPath = m_clientCache->pathForHash(hash);
    ASSERT_FALSE(cachedPath.isEmpty());
    EXPECT_TRUE(QFile::exists(cachedPath));
    EXPECT_TRUE(filesIdentical(srcPath, cachedPath));
}

// ---------------------------------------------------------------------------
// ResumeFromPartial
// ---------------------------------------------------------------------------

TEST_F(NetmixTrackTransferTest, ResumeFromPartial) {
    constexpr qint64 kFileSize = 5LL * 1024 * 1024;
    constexpr qint64 kPartialSize = 2LL * 1024 * 1024;

    QString srcPath = createTempFile(
            m_hostCacheDir->path(), QStringLiteral("resume_src.mp3"), kFileSize);
    ASSERT_FALSE(srcPath.isEmpty());
    QString hash = computeSha256Hex(srcPath);
    ASSERT_EQ(64, hash.size());

    QString ext = QStringLiteral("mp3");
    QString baseName = hash + QChar('.') + ext;
    QString partialPath =
            QDir(m_clientCache->cacheDirPath()).filePath(baseName + QStringLiteral(".partial"));

    {
        // Create partial file with first kPartialSize bytes of content
        QFile srcFile(srcPath);
        ASSERT_TRUE(srcFile.open(QIODevice::ReadOnly));

        QByteArray partialContent = srcFile.read(kPartialSize);
        ASSERT_EQ(kPartialSize, partialContent.size());
        srcFile.close();

        QFile partialFile(partialPath);
        ASSERT_TRUE(partialFile.open(QIODevice::WriteOnly));
        ASSERT_EQ(kPartialSize, partialFile.write(partialContent));
        partialFile.close();
    }

    QSignalSpy hostCompleteSpy(m_hostTransfer.get(), &TrackTransfer::complete);
    QSignalSpy hostFailedSpy(m_hostTransfer.get(), &TrackTransfer::failed);
    QSignalSpy clientReceivedSpy(
            m_clientTransfer.get(), &TrackTransfer::trackReceived);

    m_hostTransfer->sendTrack(
            srcPath, hash, QStringLiteral("resume.mp3"), QStringLiteral("audio/mpeg"));

    ASSERT_TRUE(hostCompleteSpy.wait(15000));

    EXPECT_EQ(0, hostFailedSpy.count());
    EXPECT_EQ(1, hostCompleteSpy.count());
    EXPECT_EQ(1, clientReceivedSpy.count());

    EXPECT_TRUE(m_clientCache->contains(hash));
    EXPECT_TRUE(m_clientCache->verify(hash));

    QString cachedPath = m_clientCache->pathForHash(hash);
    ASSERT_FALSE(cachedPath.isEmpty());
    EXPECT_TRUE(QFile::exists(cachedPath));
    EXPECT_EQ(kFileSize, QFileInfo(cachedPath).size());
    EXPECT_TRUE(filesIdentical(srcPath, cachedPath));
}

// ---------------------------------------------------------------------------
// CorruptedChunkTriggersReRequest
// ---------------------------------------------------------------------------

TEST_F(NetmixTrackTransferTest, CorruptedChunkTriggersReRequest) {
    constexpr qint64 kFileSize = 2LL * 1024 * 1024;

    QString srcPath = createTempFile(
            m_hostCacheDir->path(), QStringLiteral("corrupt_src.mp3"), kFileSize);
    ASSERT_FALSE(srcPath.isEmpty());
    QString hash = computeSha256Hex(srcPath);
    ASSERT_EQ(64, hash.size());

    QSignalSpy hostCompleteSpy(m_hostTransfer.get(), &TrackTransfer::complete);
    QSignalSpy hostFailedSpy(m_hostTransfer.get(), &TrackTransfer::failed);
    QSignalSpy clientReceivedSpy(
            m_clientTransfer.get(), &TrackTransfer::trackReceived);

    QSignalSpy clientProgressSpy(
            m_clientTransfer.get(), &TrackTransfer::progress);

    m_hostTransfer->sendTrack(
            srcPath, hash, QStringLiteral("corrupt.mp3"), QStringLiteral("audio/mpeg"));

    // Wait for first progress signal (at least one chunk received by client)
    ASSERT_TRUE(clientProgressSpy.wait(10000));

    // Corrupt byte 0 of the partial file
    QString ext = TrackTransfer::mimeToExt(QStringLiteral("audio/mpeg"));
    QString partialPath = QDir(m_clientCache->cacheDirPath())
                                  .filePath(hash + QChar('.') + ext + QStringLiteral(".partial"));
    QFile corruptFile(partialPath);
    ASSERT_TRUE(corruptFile.open(QIODevice::ReadWrite))
            << "partial file should exist at " << qPrintable(partialPath);
    corruptFile.seek(0);
    corruptFile.write("C", 1);
    corruptFile.close();

    // Wait for completion (corruption should trigger restart + re-verify)
    ASSERT_TRUE(hostCompleteSpy.wait(20000));

    EXPECT_EQ(0, hostFailedSpy.count());
    EXPECT_EQ(1, clientReceivedSpy.count());

    EXPECT_TRUE(m_clientCache->contains(hash));
    EXPECT_TRUE(m_clientCache->verify(hash));

    QString cachedPath = m_clientCache->pathForHash(hash);
    ASSERT_FALSE(cachedPath.isEmpty());
    EXPECT_TRUE(QFile::exists(cachedPath));
    EXPECT_TRUE(filesIdentical(srcPath, cachedPath));
}

// ---------------------------------------------------------------------------
// InterleavedControlMessages
// ---------------------------------------------------------------------------

TEST_F(NetmixTrackTransferTest, InterleavedControlMessages) {
    constexpr qint64 kFileSize = 3LL * 1024 * 1024;

    QString srcPath = createTempFile(
            m_hostCacheDir->path(), QStringLiteral("interleave_src.mp3"), kFileSize);
    ASSERT_FALSE(srcPath.isEmpty());
    QString hash = computeSha256Hex(srcPath);
    ASSERT_EQ(64, hash.size());

    bool pingReceived = false;
    QMetaObject::Connection conn = m_clientSession->connect(
            m_clientSession.get(), &TcpSession::messageReceived,
            [&](const NetmixMessage& msg) {
                if (msg.type == NetmixMessageType::Ping) {
                    const auto* ping = std::get_if<NetmixPing>(&msg.payload);
                    if (ping && ping->sentTick == 12345) {
                        pingReceived = true;
                    }
                }
            });

    QSignalSpy hostCompleteSpy(m_hostTransfer.get(), &TrackTransfer::complete);
    QSignalSpy hostFailedSpy(m_hostTransfer.get(), &TrackTransfer::failed);

    m_hostTransfer->sendTrack(
            srcPath, hash, QStringLiteral("interleave.mp3"), QStringLiteral("audio/mpeg"));

    // Let chunks start flowing, then inject a control message
    pumpEvents(500);

    NetmixPing controlPing;
    controlPing.sentTick = 12345;
    m_hostSession->sendMessage({NetmixMessageType::Ping, controlPing});

    pumpEvents(10000);

    m_clientSession->disconnect(conn);

    EXPECT_EQ(0, hostFailedSpy.count());
    EXPECT_EQ(1, hostCompleteSpy.count());
    EXPECT_TRUE(pingReceived)
            << "Control message (Ping) should arrive before transfer completes";
}

} // namespace
