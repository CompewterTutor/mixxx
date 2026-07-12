#pragma once

#include <QDir>
#include <QHash>
#include <QObject>
#include <QString>

#include <optional>

struct CacheEntry {
    QString originalFilename;
    qint64 size = 0;
    QString sourcePeer;
    quint64 addedTimestamp = 0;
    bool verified = false;
};

class TrackCache : public QObject {
    Q_OBJECT
  public:
    explicit TrackCache(const QString& settingsPath, QObject* parent = nullptr);
    ~TrackCache() override = default;

    bool initialize();

    QString cacheDirPath() const;

    static QString hashFile(const QString& filePath);

    std::optional<QString> insert(const QString& sourceFilePath);

    std::optional<CacheEntry> lookup(const QString& hash) const;

    bool verify(const QString& hash);

    bool evict(const QString& hash);

    bool contains(const QString& hash) const;

    QString pathForHash(const QString& hash) const;

    void rebuildIndex();

  private:
    static QString computeSha256(const QString& filePath);
    static QString extensionFromPath(const QString& filePath);
    QString cacheFilePath(const QString& hash, const QString& ext) const;
    bool loadIndex();
    bool saveIndex();
    bool isPathSafe(const QString& candidatePath) const;

    QDir m_cacheDir;
    QString m_indexPath;
    QHash<QString, CacheEntry> m_entries;
    qint64 m_cacheSizeBytes = 0;

    static constexpr int kIndexVersion = 1;
};
