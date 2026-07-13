#pragma once

#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QVector>

#include "netmix/channelownership.h"
#include "netmix/controlapplier.h"
#include "netmix/controlcapture.h"
#include "netmix/inputframe.h"
#include "netmix/protocol.h"
#include "netmix/quantizer.h"
#include "netmix/sessionclock.h"
#include "netmix/tcpsession.h"
#include "netmix/udpchannel.h"

class ControlObject;
class ControlProxy;
class Library;
class PlayerManager;
class TrackCache;
class TrackTransfer;

class NetmixSessionManager : public QObject {
    Q_OBJECT
  public:
    enum SessionState {
        Idle = 0,
        Connecting = 1,
        Connected = 2,
        Degraded = 3,
    };
    Q_ENUM(SessionState)

    NetmixSessionManager(QObject* parent = nullptr);
    ~NetmixSessionManager() override;

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    void setDisplayName(const QString& name);
    void setPreassignment(int channelId, int assign);

    void hostSession(quint16 port);
    void joinSession(const QHostAddress& address, quint16 port);
    void leaveSession();

    SessionState state() const { return m_state; }

    void setTrackCache(TrackCache* cache);
    void setPlayerManager(PlayerManager* pm) { m_pPlayerManager = pm; }
    void setLibrary(Library* lib) { m_pLibrary = lib; }

    void notifyTrackLoaded(int channelId,
            const QString& filePath,
            const QString& name,
            const QString& mime);

    bool isDeckReady(int channelId) const;

    // Test accessors
    TcpSession* tcpSession() const { return m_pTcpSession; }
    UdpChannel* udpChannel() const { return m_pUdpChannel; }
    SessionClock& sessionClock() { return m_sessionClock; }
    ChannelOwnership* channelOwnership() const { return m_pChannelOwnership; }

  signals:
    void sessionStateChanged(SessionState newState);
    void deckReady(int channelId);
    void rttUpdated(double rttMs);

  private slots:
    void onTcpStateChanged(TcpSession::State ts);
    void onTickAdvanced(quint32 tick);
    void onInputFrameReceived(quint32 baseTick,
            QVector<NetmixInputFrameEvent> events);
    void onHelloComplete(quint8 peerId, const QVector<quint16>& remotePreassigned);
    void onTcpMessageReceived(const NetmixMessage& msg);
    void onTrackTransferComplete(const QString& hash);
    void onTrackTransferFailed(const QString& hash, const QString& reason);
    void onTrackReceived(const QString& hash, const QString& filePath);
    void onCueSnapshotReceived(const QString& hashHex,
            const QVector<NetmixCueSnapshotEntry>& cues);

  private:
    void setState(SessionState state);
    void onTcpConnected();
    void onTcpDisconnected();
    void deleteSubComponents();
    void applyPreAssignment();
    void updateGating(int channelId);
    void loadCachedTrack(const QString& hash, const QString& filePath, quint16 channelId);

    SessionState m_state = Idle;
    bool m_enabled = false;
    QString m_displayName;
    QVector<quint16> m_preassignments;
    SessionClock m_sessionClock;
    ControlObject* m_pStatusCO;

    // Session sub-components (created on hostSession/joinSession)
    TcpSession* m_pTcpSession = nullptr;
    UdpChannel* m_pUdpChannel = nullptr;
    ControlCapture* m_pCapture = nullptr;
    ControlApplier* m_pApplier = nullptr;
    InputFramePacker* m_pPacker = nullptr;
    NetmixQuantizer* m_pQuantizer = nullptr;
    ControlObject* m_pQuantizeCO = nullptr;
    ControlProxy* m_pBpmProxy = nullptr;

    // Track transfer
    TrackTransfer* m_pTrackTransfer = nullptr;
    TrackCache* m_pTrackCache = nullptr;

    // Ready-state tracking per channel (0..4)
    QVector<bool> m_localTrackLoaded;
    QVector<bool> m_remoteReady;
    QVector<QString> m_currentHash;
    QHash<QString, quint16> m_pendingTransfers;

    // Gating: per-channel ready COs (create once) and mute proxies
    // Ready COs are created as read-only ControlObject in ctor.
    // When duplicate ctor occurs (test), later instances use a proxy.
    QVector<ControlObject*> m_pDeckReadyCOs;
    QVector<ControlProxy*> m_pDeckReadyProxies;
    QVector<ControlProxy*> m_pMuteProxies;

    // Incoming transfer hash -> channelId map (populated from TrackOffer)
    QHash<QString, quint16> m_incomingChannelMap;

    // External services needed for remote deck load and analysis
    PlayerManager* m_pPlayerManager = nullptr;
    Library* m_pLibrary = nullptr;

    // Pending cue snapshot data (receiver side, applied in loadCachedTrack)
    QHash<QString, QVector<NetmixCueSnapshotEntry>> m_pendingCueData;

    // Channel ownership
    ChannelOwnership* m_pChannelOwnership = nullptr;
    QVector<quint16> m_bufferedRemotePreassignment;
};
