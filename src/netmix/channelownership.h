#pragma once

#include <QHash>
#include <QObject>
#include <QVector>

enum class OwnershipState : quint8 {
    Unowned = 0,
    OwnedLocal = 1,
    OwnedRemote = 2,
    PendingClaim = 3,
};

class ChannelOwnership : public QObject {
    Q_OBJECT
  public:
    enum RemoteClaimAction {
        GrantAction = 0,
        DenyAction = 1,
        IgnoreAction = 2,
    };

    ChannelOwnership(quint8 selfPeerId, QObject* parent = nullptr);

    // Pre-assignment from handshake
    void setLocalPreAssignment(const QVector<quint16>& channels);
    void setRemotePreAssignment(const QVector<quint16>& channels);
    void resolvePreAssignment();

    // Local actions
    bool claim(quint16 channelId);
    void release(quint16 channelId);
    void autoReleaseAll();

    // Remote message handlers
    void handleGrant(quint16 channelId);
    void handleDeny(quint16 channelId, quint8 reason);
    void handleRelease(quint16 channelId);

    // Remote claim handling
    RemoteClaimAction handleRemoteClaim(quint16 channelId, quint8 remotePeerId);

    // Queries
    OwnershipState state(quint16 channelId) const;
    bool isOwnedByLocal(quint16 channelId) const;
    bool isOwnedByRemote(quint16 channelId) const;
    bool isOwnedByPeer(quint16 channelId, quint8 peerId) const;
    bool canClaim(quint16 channelId) const;
    quint8 ownerPeerId(quint16 channelId) const;

    // Display value mapping for COs: 0=local, 1=remote, 2=open
    static quint8 ownershipToDisplayValue(OwnershipState state);

  signals:
    void claimRequested(quint16 channelId);
    void releaseRequested(quint16 channelId);
    void ownershipChanged(quint16 channelId, OwnershipState newState);

  private:
    struct ChannelEntry {
        OwnershipState state = OwnershipState::Unowned;
        quint8 ownerPeerId = 0;
    };

    ChannelEntry& entryFor(quint16 channelId);
    const ChannelEntry& entryFor(quint16 channelId) const;
    void setState(quint16 channelId, OwnershipState newState, quint8 peerId);

    quint8 m_selfPeerId;
    QVector<ChannelEntry> m_channels;
    QVector<quint16> m_localPreAssignment;
    QVector<quint16> m_remotePreAssignment;
    bool m_preAssignmentResolved = false;
};
