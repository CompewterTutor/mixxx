#pragma once

#include <QFile>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

#include "netmix/protocol.h"

class TcpSession;
class TrackCache;

class TrackTransfer : public QObject {
    Q_OBJECT
  public:
    explicit TrackTransfer(TcpSession* session, TrackCache* cache, QObject* parent = nullptr);
    ~TrackTransfer() override;

    void sendTrack(const QString& filePath,
            const QString& hash,
            const QString& name,
            const QString& mime,
            quint16 channelId = 0);
    void cancelAll();

    static QString mimeToExt(const QString& mime);

  signals:
    void progress(const QString& hash, quint64 bytesSent, quint64 total);
    void complete(const QString& hash);
    void failed(const QString& hash, const QString& reason);
    void trackReceived(const QString& hash, const QString& filePath);

  private slots:
    void onMessageReceived(const NetmixMessage& msg);
    void sendNextBatch();

  private:
    struct OutgoingTransfer {
        QFile* file = nullptr;
        QString hash;
        QByteArray hashRaw;
        quint64 totalSize = 0;
        quint64 bytesSent = 0;
    };

    struct IncomingTransfer {
        QFile* file = nullptr;
        QString hash;
        QByteArray hashRaw;
        quint64 totalSize = 0;
        quint64 bytesReceived = 0;
        QString partialPath;
    };

    void handleTrackOffer(const NetmixTrackOffer& offer);
    void handleTrackAccept(const NetmixTrackAccept& accept);
    void handleTrackChunk(const NetmixTrackChunk& chunk);
    void handleTrackComplete(const NetmixTrackComplete& complete);
    void handleTrackReady(const NetmixTrackReady& ready);

    void cleanupOutgoing(const QString& hashHex);
    void cleanupIncoming(const QString& hashHex);

    TcpSession* m_pSession;
    TrackCache* m_pCache;
    QHash<QString, OutgoingTransfer> m_outgoing;
    QHash<QString, IncomingTransfer> m_incoming;

    static constexpr qint64 kChunkSize = 65536;
    static constexpr int kMaxChunksPerBatch = 4;
};
