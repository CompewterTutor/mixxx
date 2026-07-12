#include "netmix/tracktransfer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>

#include "netmix/tcpsession.h"
#include "netmix/trackcache.h"

namespace {

const QString kExtMp3  = QStringLiteral("mp3");
const QString kExtFlac = QStringLiteral("flac");
const QString kExtOgg  = QStringLiteral("ogg");
const QString kExtWav  = QStringLiteral("wav");
const QString kExtAiff = QStringLiteral("aiff");
const QString kExtM4a  = QStringLiteral("m4a");
const QString kExtBin  = QStringLiteral("bin");
const QString kExtPartial = QStringLiteral(".partial");

QString mimeToExtImpl(const QString& mime) {
    if (mime == QLatin1String("audio/mpeg"))  return kExtMp3;
    if (mime == QLatin1String("audio/flac"))  return kExtFlac;
    if (mime == QLatin1String("audio/ogg"))   return kExtOgg;
    if (mime == QLatin1String("audio/vorbis")) return kExtOgg;
    if (mime == QLatin1String("audio/wav"))   return kExtWav;
    if (mime == QLatin1String("audio/wave"))  return kExtWav;
    if (mime == QLatin1String("audio/x-wav")) return kExtWav;
    if (mime == QLatin1String("audio/aiff"))  return kExtAiff;
    if (mime == QLatin1String("audio/x-aiff")) return kExtAiff;
    if (mime == QLatin1String("audio/x-m4a")) return kExtM4a;
    if (mime == QLatin1String("audio/mp4"))   return kExtM4a;
    return kExtBin;
}

} // anonymous namespace

TrackTransfer::TrackTransfer(TcpSession* session, TrackCache* cache, QObject* parent)
        : QObject(parent)
        , m_pSession(session)
        , m_pCache(cache) {
    connect(m_pSession, &TcpSession::messageReceived,
            this, &TrackTransfer::onMessageReceived);
}

TrackTransfer::~TrackTransfer() {
    cancelAll();
}

void TrackTransfer::sendTrack(
        const QString& filePath,
        const QString& hash,
        const QString& name,
        const QString& mime) {
    QFile* file = new QFile(filePath, this);
    if (!file->open(QIODevice::ReadOnly)) {
        qWarning("[Netmix] TrackTransfer::sendTrack: cannot open %s",
                 qPrintable(filePath));
        emit failed(hash, QStringLiteral("cannot open source file"));
        delete file;
        return;
    }

    OutgoingTransfer ot;
    ot.file = file;
    ot.hash = hash;
    ot.hashRaw = QByteArray::fromHex(hash.toLatin1());
    ot.totalSize = file->size();
    ot.bytesSent = 0;

    m_outgoing.insert(hash, ot);

    NetmixTrackOffer offer;
    offer.hash = ot.hashRaw;
    offer.size = ot.totalSize;
    offer.name = name;
    offer.mime = mime;

    m_pSession->sendMessage({NetmixMessageType::TrackOffer, offer});
}

void TrackTransfer::cancelAll() {
    auto outgoingHashes = m_outgoing.keys();
    for (const QString& hash : outgoingHashes) {
        cleanupOutgoing(hash);
    }

    auto incomingHashes = m_incoming.keys();
    for (const QString& hash : incomingHashes) {
        cleanupIncoming(hash);
    }
}

void TrackTransfer::onMessageReceived(const NetmixMessage& msg) {
    switch (msg.type) {
    case NetmixMessageType::TrackOffer:
        if (const auto* p = std::get_if<NetmixTrackOffer>(&msg.payload)) {
            handleTrackOffer(*p);
        }
        break;
    case NetmixMessageType::TrackAccept:
        if (const auto* p = std::get_if<NetmixTrackAccept>(&msg.payload)) {
            handleTrackAccept(*p);
        }
        break;
    case NetmixMessageType::TrackChunk:
        if (const auto* p = std::get_if<NetmixTrackChunk>(&msg.payload)) {
            handleTrackChunk(*p);
        }
        break;
    case NetmixMessageType::TrackComplete:
        if (const auto* p = std::get_if<NetmixTrackComplete>(&msg.payload)) {
            handleTrackComplete(*p);
        }
        break;
    case NetmixMessageType::TrackReady:
        if (const auto* p = std::get_if<NetmixTrackReady>(&msg.payload)) {
            handleTrackReady(*p);
        }
        break;
    default:
        break;
    }
}

void TrackTransfer::sendNextBatch() {
    // Find an active outgoing transfer with remaining data
    OutgoingTransfer* ot = nullptr;
    for (auto& kv : m_outgoing) {
        if (kv.file && kv.file->isOpen() && kv.bytesSent < kv.totalSize) {
            ot = &kv;
            break;
        }
    }
    if (!ot) {
        return;
    }

    int sentInBatch = 0;
    while (sentInBatch < kMaxChunksPerBatch && ot->bytesSent < ot->totalSize) {
        qint64 remaining = ot->totalSize - ot->bytesSent;
        qint64 readSize = qMin(remaining, kChunkSize);

        QByteArray buf;
        buf.resize(static_cast<int>(readSize));
        qint64 actualRead = ot->file->read(buf.data(), readSize);
        if (actualRead <= 0) {
            qWarning("[Netmix] TrackTransfer::sendNextBatch: read error at offset %llu",
                     ot->bytesSent);
            emit failed(ot->hash, QStringLiteral("read error"));
            cleanupOutgoing(ot->hash);
            return;
        }
        buf.resize(static_cast<int>(actualRead));

        NetmixTrackChunk chunk;
        chunk.hash = ot->hashRaw;
        chunk.offset = ot->bytesSent;
        chunk.data = buf;

        m_pSession->sendMessage({NetmixMessageType::TrackChunk, chunk});

        ot->bytesSent += static_cast<quint64>(actualRead);
        sentInBatch++;
    }

    emit progress(ot->hash, ot->bytesSent, ot->totalSize);

    if (ot->bytesSent >= ot->totalSize) {
        // All chunks sent — send TrackComplete
        NetmixTrackComplete complete;
        complete.hash = ot->hashRaw;
        m_pSession->sendMessage({NetmixMessageType::TrackComplete, complete});
    } else {
        // More chunks remain — yield to event loop for control messages
        QTimer::singleShot(0, this, &TrackTransfer::sendNextBatch);
    }
}

// ---------------------------------------------------------------------------
// Incoming message handlers
// ---------------------------------------------------------------------------

void TrackTransfer::handleTrackOffer(const NetmixTrackOffer& offer) {
    QString hashHex = QString::fromLatin1(offer.hash.toHex());

    if (m_incoming.contains(hashHex) || m_outgoing.contains(hashHex)) {
        return;
    }

    if (m_pCache->contains(hashHex)) {
        NetmixTrackReady ready;
        ready.hash = offer.hash;
        m_pSession->sendMessage({NetmixMessageType::TrackReady, ready});
        return;
    }

    // Check for partial file from interrupted transfer
    quint64 haveBytes = 0;
    QString ext = mimeToExtImpl(offer.mime.isEmpty() ? QStringLiteral("application/octet-stream") : offer.mime);
    QString baseName = hashHex + QChar('.') + ext;
    QString partialPath = QDir(m_pCache->cacheDirPath()).filePath(baseName + kExtPartial);
    QString finalPath = QDir(m_pCache->cacheDirPath()).filePath(baseName);

    // Clean up any existing partial file
    if (QFile::exists(partialPath)) {
        QFileInfo partialInfo(partialPath);
        haveBytes = static_cast<quint64>(partialInfo.size());
        if (haveBytes >= offer.size) {
            // Partial is already complete or malformed — start fresh
            QFile::remove(partialPath);
            haveBytes = 0;
        }
    }

    QFile* partialFile = new QFile(partialPath, this);
    QIODevice::OpenMode mode = (haveBytes > 0) ? QIODevice::Append : QIODevice::WriteOnly;
    if (!partialFile->open(mode)) {
        qWarning("[Netmix] TrackTransfer::handleTrackOffer: cannot open partial %s",
                 qPrintable(partialPath));
        emit failed(hashHex, QStringLiteral("cannot create partial file"));
        delete partialFile;
        return;
    }

    if (haveBytes > 0 && mode == QIODevice::Append) {
        partialFile->seek(haveBytes);
    }

    IncomingTransfer it;
    it.file = partialFile;
    it.hash = hashHex;
    it.hashRaw = offer.hash;
    it.totalSize = offer.size;
    it.bytesReceived = haveBytes;
    it.partialPath = partialPath;

    m_incoming.insert(hashHex, it);

    NetmixTrackAccept accept;
    accept.hash = offer.hash;
    accept.haveBytes = haveBytes;
    m_pSession->sendMessage({NetmixMessageType::TrackAccept, accept});
}

void TrackTransfer::handleTrackAccept(const NetmixTrackAccept& accept) {
    QString hashHex = QString::fromLatin1(accept.hash.toHex());
    auto it = m_outgoing.find(hashHex);
    if (it == m_outgoing.end()) {
        return;
    }

    // Seek to resume point
    if (accept.haveBytes > 0) {
        if (!it->file || !it->file->seek(static_cast<qint64>(accept.haveBytes))) {
            qWarning("[Netmix] TrackTransfer::handleTrackAccept: seek to %llu failed for %s",
                     accept.haveBytes, qPrintable(hashHex));
            emit failed(hashHex, QStringLiteral("seek error"));
            cleanupOutgoing(hashHex);
            return;
        }
        it->bytesSent = accept.haveBytes;
    } else {
        // Restart from beginning
        if (it->file && it->file->isOpen()) {
            it->file->seek(0);
        }
        it->bytesSent = 0;
    }

    sendNextBatch();
}

void TrackTransfer::handleTrackChunk(const NetmixTrackChunk& chunk) {
    QString hashHex = QString::fromLatin1(chunk.hash.toHex());
    auto it = m_incoming.find(hashHex);
    if (it == m_incoming.end()) {
        // Could be stale chunk after restart — ignore
        return;
    }

    IncomingTransfer& inc = it.value();

    // Validate offset matches expected position
    if (chunk.offset != inc.bytesReceived) {
        // Offset mismatch — request restart from current confirmed position
        qWarning("[Netmix] TrackTransfer::handleTrackChunk: offset mismatch for %s "
                 "(expected %llu, got %llu), resetting",
                 qPrintable(hashHex), inc.bytesReceived, chunk.offset);
        NetmixTrackAccept accept;
        accept.hash = inc.hashRaw;
        accept.haveBytes = inc.bytesReceived;
        m_pSession->sendMessage({NetmixMessageType::TrackAccept, accept});
        return;
    }

    // Write chunk data to partial file
    qint64 written = inc.file->write(chunk.data);
    if (written != chunk.data.size()) {
        qWarning("[Netmix] TrackTransfer::handleTrackChunk: write error for %s",
                 qPrintable(hashHex));
        emit failed(hashHex, QStringLiteral("write error"));
        cleanupIncoming(hashHex);
        return;
    }

    inc.bytesReceived += static_cast<quint64>(written);

    emit progress(hashHex, inc.bytesReceived, inc.totalSize);
}

void TrackTransfer::handleTrackComplete(const NetmixTrackComplete& complete) {
    QString hashHex = QString::fromLatin1(complete.hash.toHex());
    auto it = m_incoming.find(hashHex);
    if (it == m_incoming.end()) {
        return;
    }

    IncomingTransfer& inc = it.value();

    // Close the partial file
    inc.file->close();

    // Verify SHA-256 of the received file
    QFile verifyFile(inc.partialPath);
    if (!verifyFile.open(QIODevice::ReadOnly)) {
        qWarning("[Netmix] TrackTransfer::handleTrackComplete: cannot open %s for verify",
                 qPrintable(inc.partialPath));
        emit failed(hashHex, QStringLiteral("verify open failed"));
        cleanupIncoming(hashHex);
        return;
    }
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    if (!hasher.addData(&verifyFile)) {
        qWarning("[Netmix] TrackTransfer::handleTrackComplete: hash read error for %s",
                 qPrintable(inc.partialPath));
        emit failed(hashHex, QStringLiteral("verify read error"));
        cleanupIncoming(hashHex);
        return;
    }
    verifyFile.close();
    QString actualHash = QString::fromLatin1(hasher.result().toHex());
    if (actualHash.isEmpty()) {
        qWarning("[Netmix] TrackTransfer::handleTrackComplete: cannot compute hash for %s",
                 qPrintable(inc.partialPath));
        emit failed(hashHex, QStringLiteral("hash computation failed"));
        cleanupIncoming(hashHex);
        return;
    }

    if (actualHash != hashHex) {
        qWarning("[Netmix] TrackTransfer::handleTrackComplete: hash mismatch for %s "
                 "(computed %s), restarting",
                 qPrintable(hashHex), qPrintable(actualHash));
        // Reset IncomingTransfer for re-receive — keep map entry so
        // subsequent TrackChunks find it.
        inc.file->close();
        inc.file->deleteLater();
        inc.file = nullptr;
        QFile::remove(inc.partialPath);
        inc.bytesReceived = 0;

        QFile* newFile = new QFile(inc.partialPath, this);
        if (newFile->open(QIODevice::WriteOnly)) {
            inc.file = newFile;
        } else {
            qWarning("[Netmix] TrackTransfer::handleTrackComplete: cannot recreate %s",
                     qPrintable(inc.partialPath));
            emit failed(hashHex, QStringLiteral("cannot recreate partial"));
            cleanupIncoming(hashHex);
            return;
        }

        NetmixTrackAccept accept;
        accept.hash = complete.hash;
        accept.haveBytes = 0;
        m_pSession->sendMessage({NetmixMessageType::TrackAccept, accept});
        return;
    }

    // Hash matches — rename .partial to final
    QString finalPath = inc.partialPath.chopped(kExtPartial.size());
    if (QFile::exists(finalPath)) {
        QFile::remove(finalPath);
    }
    if (!QFile::rename(inc.partialPath, finalPath)) {
        qWarning("[Netmix] TrackTransfer::handleTrackComplete: rename failed %s -> %s",
                 qPrintable(inc.partialPath), qPrintable(finalPath));
        emit failed(hashHex, QStringLiteral("rename failed"));
        cleanupIncoming(hashHex);
        return;
    }

    // Insert into cache
    auto insertedHash = m_pCache->insert(finalPath);
    if (!insertedHash.has_value()) {
        qWarning("[Netmix] TrackTransfer::handleTrackComplete: cache insert failed for %s",
                 qPrintable(finalPath));
        emit failed(hashHex, QStringLiteral("cache insert failed"));
        cleanupIncoming(hashHex);
        return;
    }

    cleanupIncoming(hashHex);

    // Send TrackReady
    NetmixTrackReady ready;
    ready.hash = complete.hash;
    m_pSession->sendMessage({NetmixMessageType::TrackReady, ready});

    emit trackReceived(hashHex, finalPath);
}

void TrackTransfer::handleTrackReady(const NetmixTrackReady& ready) {
    QString hashHex = QString::fromLatin1(ready.hash.toHex());
    auto it = m_outgoing.find(hashHex);
    if (it == m_outgoing.end()) {
        return;
    }

    cleanupOutgoing(hashHex);
    emit complete(hashHex);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void TrackTransfer::cleanupOutgoing(const QString& hashHex) {
    auto it = m_outgoing.find(hashHex);
    if (it == m_outgoing.end()) {
        return;
    }
    if (it->file) {
        it->file->close();
        it->file->deleteLater();
        it->file = nullptr;
    }
    m_outgoing.erase(it);
}

void TrackTransfer::cleanupIncoming(const QString& hashHex) {
    auto it = m_incoming.find(hashHex);
    if (it == m_incoming.end()) {
        return;
    }
    if (it->file) {
        it->file->close();
        it->file->deleteLater();
        it->file = nullptr;
    }
    m_incoming.erase(it);
}

QString TrackTransfer::mimeToExt(const QString& mime) {
    return mimeToExtImpl(mime);
}

#include "moc_tracktransfer.cpp"
