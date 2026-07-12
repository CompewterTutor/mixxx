#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "netmix/trackcache.h"
#include "test/mixxxtest.h"

namespace {

class NetmixTrackCacheTest : public MixxxTest {
  protected:
    void SetUp() override {
        m_tempDir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(m_tempDir->isValid());
        m_cache = std::make_unique<TrackCache>(m_tempDir->path());
        ASSERT_TRUE(m_cache->initialize());
    }

    void TearDown() override {
        m_cache.reset();
        m_tempDir.reset();
    }

    QString writeSourceFile(const QString& fileName, const QByteArray& content) {
        QString path = QDir(m_tempDir->path()).filePath(fileName);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(content);
            file.close();
        }
        return path;
    }

    void corruptIndex() {
        QFile idx(QDir(m_cache->cacheDirPath()).filePath(QStringLiteral("index.json")));
        if (idx.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            idx.write("garbage data not json");
            idx.close();
        }
    }

    void removeIndex() {
        QFile::remove(QDir(m_cache->cacheDirPath()).filePath(QStringLiteral("index.json")));
    }

    std::unique_ptr<QTemporaryDir> m_tempDir;
    std::unique_ptr<TrackCache> m_cache;
};

TEST_F(NetmixTrackCacheTest, InsertLookupRoundTrip) {
    QByteArray content = "Hello TrackCache World!";
    QString srcPath = writeSourceFile("test_song.mp3", content);

    auto hashOpt = m_cache->insert(srcPath);
    ASSERT_TRUE(hashOpt.has_value());
    QString hash = hashOpt.value();

    auto entryOpt = m_cache->lookup(hash);
    ASSERT_TRUE(entryOpt.has_value());
    EXPECT_EQ(entryOpt->size, content.size());
    EXPECT_EQ(entryOpt->originalFilename, QStringLiteral("test_song.mp3"));
    EXPECT_TRUE(entryOpt->verified);

    QString cachedPath = m_cache->pathForHash(hash);
    EXPECT_FALSE(cachedPath.isEmpty());
    EXPECT_TRUE(QFile::exists(cachedPath));

    EXPECT_TRUE(m_cache->contains(hash));
}

TEST_F(NetmixTrackCacheTest, VerifyPass) {
    QByteArray content = "Data for verify test";
    QString srcPath = writeSourceFile("verify_me.flac", content);
    auto hashOpt = m_cache->insert(srcPath);
    ASSERT_TRUE(hashOpt.has_value());
    QString hash = hashOpt.value();

    EXPECT_TRUE(m_cache->verify(hash));

    QString cachedPath = m_cache->pathForHash(hash);
    ASSERT_FALSE(cachedPath.isEmpty());

    QFile file(cachedPath);
    ASSERT_TRUE(file.open(QIODevice::Append));
    file.write("corruption");
    file.close();

    EXPECT_FALSE(m_cache->verify(hash));
}

TEST_F(NetmixTrackCacheTest, EvictRemovesFile) {
    QByteArray content = "Evict me";
    QString srcPath = writeSourceFile("evict_me.wav", content);
    auto hashOpt = m_cache->insert(srcPath);
    ASSERT_TRUE(hashOpt.has_value());
    QString hash = hashOpt.value();

    QString cachedPath = m_cache->pathForHash(hash);
    EXPECT_TRUE(QFile::exists(cachedPath));

    EXPECT_TRUE(m_cache->evict(hash));

    EXPECT_FALSE(QFile::exists(cachedPath));
    EXPECT_FALSE(m_cache->contains(hash));
}

TEST_F(NetmixTrackCacheTest, RebuildFromScratch) {
    QByteArray content1 = "Track one data";
    QByteArray content2 = "Track two data";
    QString src1 = writeSourceFile("track1.ogg", content1);
    QString src2 = writeSourceFile("track2.ogg", content2);

    auto hash1 = m_cache->insert(src1);
    auto hash2 = m_cache->insert(src2);
    ASSERT_TRUE(hash1.has_value());
    ASSERT_TRUE(hash2.has_value());

    removeIndex();

    m_cache.reset();
    m_cache = std::make_unique<TrackCache>(m_tempDir->path());
    ASSERT_TRUE(m_cache->initialize());

    EXPECT_TRUE(m_cache->contains(hash1.value()));
    EXPECT_TRUE(m_cache->contains(hash2.value()));

    EXPECT_TRUE(m_cache->verify(hash1.value()));
    EXPECT_TRUE(m_cache->verify(hash2.value()));
}

TEST_F(NetmixTrackCacheTest, RebuildCorruptIndex) {
    QByteArray content = "Survive corruption";
    QString srcPath = writeSourceFile("survivor.aiff", content);
    auto hashOpt = m_cache->insert(srcPath);
    ASSERT_TRUE(hashOpt.has_value());
    QString hash = hashOpt.value();

    corruptIndex();

    m_cache.reset();
    m_cache = std::make_unique<TrackCache>(m_tempDir->path());
    ASSERT_TRUE(m_cache->initialize());

    EXPECT_TRUE(m_cache->contains(hash));
    EXPECT_TRUE(m_cache->verify(hash));
}

TEST_F(NetmixTrackCacheTest, TraversalRejected) {
    QString badPath = m_cache->cacheDirPath() + QStringLiteral("/../../nonexistent/../evil.mp3");
    auto result = m_cache->insert(badPath);
    EXPECT_FALSE(result.has_value());

    EXPECT_TRUE(m_cache->pathForHash(QStringLiteral("../../etc/passwd")).isEmpty());

    EXPECT_FALSE(m_cache->lookup(QStringLiteral("../../etc/passwd")).has_value());

    QDir cacheDir(m_cache->cacheDirPath());
    QStringList files = cacheDir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QString& f : files) {
        QString fullPath = cacheDir.filePath(f);
        EXPECT_TRUE(fullPath.startsWith(m_cache->cacheDirPath()));
    }
}

} // namespace
