#include "netmix/trackcache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>

namespace {
const QString kIndexFileName = QStringLiteral("index.json");
const QString kCacheDirName = QStringLiteral("netmix_cache");

} // anonymous namespace

TrackCache::TrackCache(const QString& settingsPath, QObject* parent)
        : QObject(parent) {
    m_cacheDir = QDir(settingsPath).filePath(kCacheDirName);
    m_indexPath = QDir(m_cacheDir).filePath(kIndexFileName);
}

bool TrackCache::initialize() {
    if (!m_cacheDir.mkpath(QStringLiteral("."))) {
        qWarning() << "[Netmix] TrackCache initialize: failed to create cache directory" << m_cacheDir.path();
        return false;
    }

    if (!loadIndex()) {
        qWarning() << "[Netmix] TrackCache initialize: index load failed, rebuilding from directory scan";
        rebuildIndex();
    }

    return true;
}

QString TrackCache::cacheDirPath() const {
    return m_cacheDir.path();
}

std::optional<QString> TrackCache::insert(const QString& sourceFilePath) {
    QFileInfo srcInfo(sourceFilePath);
    if (!srcInfo.exists() || !srcInfo.isReadable()) {
        qWarning() << "[Netmix] TrackCache insert failed: source not readable"
                    << sourceFilePath;
        return std::nullopt;
    }

    QString hash = computeSha256(sourceFilePath);
    if (hash.isEmpty()) {
        return std::nullopt;
    }

    QString ext = extensionFromPath(sourceFilePath);
    QString destPath = cacheFilePath(hash, ext);

    if (!QFile::exists(destPath)) {
        if (!QFile::copy(sourceFilePath, destPath)) {
            qWarning() << "[Netmix] TrackCache insert failed: copy error"
                        << sourceFilePath << "->" << destPath;
            return std::nullopt;
        }
    }

    CacheEntry entry;
    entry.originalFilename = srcInfo.fileName();
    entry.size = srcInfo.size();
    entry.sourcePeer = QString(); // set by caller later
    entry.addedTimestamp = QDateTime::currentSecsSinceEpoch();
    entry.verified = true;

    m_entries.insert(hash, entry);
    m_cacheSizeBytes += entry.size;

    if (!saveIndex()) {
        qWarning() << "[Netmix] TrackCache insert: index save failed";
    }

    return hash;
}

std::optional<CacheEntry> TrackCache::lookup(const QString& hash) const {
    auto it = m_entries.constFind(hash);
    if (it == m_entries.constEnd()) {
        return std::nullopt;
    }
    return it.value();
}

bool TrackCache::verify(const QString& hash) {
    auto it = m_entries.find(hash);
    if (it == m_entries.end()) {
        return false;
    }

    QString cachedPath = pathForHash(hash);
    if (cachedPath.isEmpty() || !QFile::exists(cachedPath)) {
        it->verified = false;
        saveIndex();
        return false;
    }

    QString actualHash = computeSha256(cachedPath);
    bool match = (actualHash == hash);

    it->verified = match;
    saveIndex();

    return match;
}

bool TrackCache::evict(const QString& hash) {
    auto it = m_entries.find(hash);
    if (it == m_entries.end()) {
        return false;
    }

    QString cachedPath = pathForHash(hash);
    if (!cachedPath.isEmpty() && QFile::exists(cachedPath)) {
        if (!QFile::remove(cachedPath)) {
            qWarning() << "[Netmix] TrackCache evict: failed to remove file"
                        << cachedPath;
        }
    }

    m_cacheSizeBytes -= it->size;
    m_entries.erase(it);

    saveIndex();
    return true;
}

bool TrackCache::contains(const QString& hash) const {
    return m_entries.contains(hash);
}

QString TrackCache::pathForHash(const QString& hash) const {
    auto it = m_entries.constFind(hash);
    if (it == m_entries.constEnd()) {
        return QString();
    }

    QString ext = extensionFromPath(it->originalFilename);
    QString path = cacheFilePath(hash, ext);

    if (!isPathSafe(path)) {
        qWarning() << "[Netmix] TrackCache pathForHash: unsafe path rejected" << path;
        return QString();
    }

    return path;
}

void TrackCache::rebuildIndex() {
    m_entries.clear();
    m_cacheSizeBytes = 0;

    QStringList files = m_cacheDir.entryList(
            QDir::Files | QDir::NoDotAndDotDot);

    for (const QString& fileName : files) {
        if (fileName == kIndexFileName) {
            continue;
        }

        QFileInfo fi(m_cacheDir.filePath(fileName));
        QString baseName = fi.completeBaseName();

        if (baseName.length() != 64) {
            continue;
        }

        bool allHex = true;
        for (const QChar& c : baseName) {
            if (!c.isDigit() && !(c.toLower() >= QChar('a') && c.toLower() <= QChar('f'))) {
                allHex = false;
                break;
            }
        }
        if (!allHex) {
            continue;
        }

        CacheEntry entry;
        entry.originalFilename = fileName;
        entry.size = fi.size();
        entry.sourcePeer = QString();
        entry.addedTimestamp = QDateTime::currentSecsSinceEpoch();
        entry.verified = true;

        m_entries.insert(baseName, entry);
        m_cacheSizeBytes += entry.size;
    }

    saveIndex();

    qInfo() << "[Netmix] TrackCache rebuildIndex:" << m_entries.size()
            << "entries, size=" << m_cacheSizeBytes;
}

QString TrackCache::hashFile(const QString& filePath) {
    return computeSha256(filePath);
}

QString TrackCache::computeSha256(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[Netmix] TrackCache computeSha256: cannot open" << filePath;
        return QString();
    }

    QCryptographicHash hasher(QCryptographicHash::Sha256);
    if (!hasher.addData(&file)) {
        qWarning() << "[Netmix] TrackCache computeSha256: read error" << filePath;
        return QString();
    }

    return QString::fromLatin1(hasher.result().toHex());
}

QString TrackCache::extensionFromPath(const QString& filePath) {
    QString suffix = QFileInfo(filePath).suffix();
    if (suffix.isEmpty()) {
        return QStringLiteral("bin");
    }
    return suffix;
}

QString TrackCache::cacheFilePath(const QString& hash, const QString& ext) const {
    return m_cacheDir.filePath(hash + QChar('.') + ext);
}

bool TrackCache::loadIndex() {
    QFile file(m_indexPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "[Netmix] TrackCache loadIndex: JSON parse error"
                    << parseError.errorString();
        return false;
    }

    if (!doc.isObject()) {
        return false;
    }

    QJsonObject root = doc.object();
    int version = root.value(QStringLiteral("version")).toInt();
    if (version != kIndexVersion) {
        qWarning() << "[Netmix] TrackCache loadIndex: unknown version" << version;
        return false;
    }

    QJsonObject entriesObj = root.value(QStringLiteral("entries")).toObject();
    m_entries.clear();
    m_cacheSizeBytes = 0;

    for (auto it = entriesObj.constBegin(); it != entriesObj.constEnd(); ++it) {
        const QString& hash = it.key();
        QJsonObject entryObj = it.value().toObject();

        CacheEntry entry;
        entry.originalFilename = entryObj.value(QStringLiteral("originalFilename")).toString();
        entry.size = static_cast<qint64>(entryObj.value(QStringLiteral("size")).toDouble());
        entry.sourcePeer = entryObj.value(QStringLiteral("sourcePeer")).toString();
        entry.addedTimestamp = static_cast<quint64>(entryObj.value(QStringLiteral("addedTimestamp")).toDouble());
        entry.verified = entryObj.value(QStringLiteral("verified")).toBool();

        m_entries.insert(hash, entry);
        m_cacheSizeBytes += entry.size;
    }

    return true;
}

bool TrackCache::saveIndex() {
    QJsonObject entriesObj;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        QJsonObject entryObj;
        entryObj[QStringLiteral("originalFilename")] = it->originalFilename;
        entryObj[QStringLiteral("size")] = static_cast<double>(it->size);
        entryObj[QStringLiteral("sourcePeer")] = it->sourcePeer;
        entryObj[QStringLiteral("addedTimestamp")] = static_cast<double>(it->addedTimestamp);
        entryObj[QStringLiteral("verified")] = it->verified;
        entriesObj[it.key()] = entryObj;
    }

    QJsonObject root;
    root[QStringLiteral("version")] = kIndexVersion;
    root[QStringLiteral("entries")] = entriesObj;

    QJsonDocument doc(root);

    QFile file(m_indexPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[Netmix] TrackCache saveIndex: cannot write" << m_indexPath;
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool TrackCache::isPathSafe(const QString& candidatePath) const {
    QString canonical = m_cacheDir.canonicalPath();
    if (canonical.isEmpty()) {
        return false;
    }

    QString cleaned = QDir::cleanPath(candidatePath);
    if (cleaned == canonical) {
        return true;
    }
    if (!canonical.endsWith(QChar('/'))) {
        canonical.append(QChar('/'));
    }
    return cleaned.startsWith(canonical);
}

#include "moc_trackcache.cpp"
