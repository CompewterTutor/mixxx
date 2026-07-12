#include "netmix/channelownership.h"

#include "util/logger.h"

namespace {
const mixxx::Logger kLogger("ChannelOwnership");
} // namespace

ChannelOwnership::ChannelOwnership(quint8 selfPeerId, QObject* parent)
        : QObject(parent),
          m_selfPeerId(selfPeerId) {
    // Max 5 channels (4 decks + 1 crossfader, channelId 0-4)
    m_channels.resize(5);
}

void ChannelOwnership::setLocalPreAssignment(const QVector<quint16>& channels) {
    m_localPreAssignment = channels;
}

void ChannelOwnership::setRemotePreAssignment(const QVector<quint16>& channels) {
    m_remotePreAssignment = channels;
}

void ChannelOwnership::resolvePreAssignment() {
    if (m_preAssignmentResolved) {
        return;
    }

    // Compute remote peerId: in 2-peer model, always opposite of self
    quint8 remotePeerId = (m_selfPeerId == 0) ? 1 : 0;

    // Apply local pre-assignment — channels this peer claims
    for (quint16 ch : m_localPreAssignment) {
        if (ch < static_cast<quint16>(m_channels.size())) {
            setState(ch, OwnershipState::OwnedLocal, m_selfPeerId);
        }
    }

    // Apply remote pre-assignment — channels remote peer claims
    for (quint16 ch : m_remotePreAssignment) {
        if (ch >= static_cast<quint16>(m_channels.size())) {
            continue;
        }
        // If both sides claim same channel: lower peerId wins
        if (m_channels[ch].state == OwnershipState::OwnedLocal) {
            if (m_selfPeerId > remotePeerId) {
                // Remote has lower peerId — they win
                setState(ch, OwnershipState::OwnedRemote, remotePeerId);
            }
            // else we keep it (we have lower peerId — we win)
        } else {
            setState(ch, OwnershipState::OwnedRemote, remotePeerId);
        }
    }

    m_preAssignmentResolved = true;
}

bool ChannelOwnership::claim(quint16 channelId) {
    if (channelId >= static_cast<quint16>(m_channels.size())) {
        qWarning("[Netmix] ChannelOwnership::claim — invalid channelId %u", channelId);
        return false;
    }

    ChannelEntry& entry = m_channels[channelId];
    if (entry.state == OwnershipState::Unowned) {
        setState(channelId, OwnershipState::PendingClaim, m_selfPeerId);
        emit claimRequested(channelId);
        return true;
    }

    if (entry.state == OwnershipState::OwnedLocal) {
        qWarning("[Netmix] ChannelOwnership::claim — already owned locally (ch %u)", channelId);
        return false;
    }

    if (entry.state == OwnershipState::OwnedRemote) {
        qWarning("[Netmix] ChannelOwnership::claim — owned by remote (ch %u)", channelId);
        return false;
    }

    // PendingClaim
    return false;
}

void ChannelOwnership::release(quint16 channelId) {
    if (channelId >= static_cast<quint16>(m_channels.size())) {
        return;
    }

    ChannelEntry& entry = m_channels[channelId];
    if (entry.state == OwnershipState::OwnedLocal ||
            entry.state == OwnershipState::PendingClaim) {
        setState(channelId, OwnershipState::Unowned, 0);
        emit releaseRequested(channelId);
    }
}

void ChannelOwnership::autoReleaseAll() {
    for (int i = 0; i < m_channels.size(); ++i) {
        ChannelEntry& entry = m_channels[i];
        if (entry.state == OwnershipState::OwnedLocal ||
                entry.state == OwnershipState::PendingClaim) {
            setState(static_cast<quint16>(i), OwnershipState::Unowned, 0);
        } else if (entry.state == OwnershipState::OwnedRemote) {
            setState(static_cast<quint16>(i), OwnershipState::Unowned, 0);
        }
    }
}

void ChannelOwnership::handleGrant(quint16 channelId) {
    if (channelId >= static_cast<quint16>(m_channels.size())) {
        return;
    }

    ChannelEntry& entry = m_channels[channelId];
    if (entry.state == OwnershipState::PendingClaim) {
        setState(channelId, OwnershipState::OwnedLocal, m_selfPeerId);
    }
}

void ChannelOwnership::handleDeny(quint16 channelId, quint8 reason) {
    Q_UNUSED(reason);
    if (channelId >= static_cast<quint16>(m_channels.size())) {
        return;
    }

    ChannelEntry& entry = m_channels[channelId];
    if (entry.state == OwnershipState::PendingClaim) {
        setState(channelId, OwnershipState::Unowned, 0);
    }
}

void ChannelOwnership::handleRelease(quint16 channelId) {
    if (channelId >= static_cast<quint16>(m_channels.size())) {
        return;
    }

    ChannelEntry& entry = m_channels[channelId];
    if (entry.state == OwnershipState::OwnedRemote) {
        setState(channelId, OwnershipState::Unowned, 0);
    }
}

ChannelOwnership::RemoteClaimAction ChannelOwnership::handleRemoteClaim(
        quint16 channelId, quint8 remotePeerId) {
    if (channelId >= static_cast<quint16>(m_channels.size())) {
        return IgnoreAction;
    }

    if (remotePeerId == m_selfPeerId) {
        return IgnoreAction;
    }

    ChannelEntry& entry = m_channels[channelId];
    if (entry.state == OwnershipState::Unowned) {
        // Auto-grant to remote
        setState(channelId, OwnershipState::OwnedRemote, remotePeerId);
        return GrantAction;
    }

    if (entry.state == OwnershipState::OwnedLocal) {
        return DenyAction;
    }

    // Race resolution: both sides claim simultaneously
    if (entry.state == OwnershipState::PendingClaim) {
        if (remotePeerId < m_selfPeerId) {
            // Remote has lower peerId — they win
            // Convert our PendingClaim to Denied locally
            setState(channelId, OwnershipState::Unowned, 0);
            // Then grant to remote
            setState(channelId, OwnershipState::OwnedRemote, remotePeerId);
            return GrantAction;
        } else {
            // We have lower (or equal) peerId — we win, deny remote
            return DenyAction;
        }
    }

    return IgnoreAction;
}

OwnershipState ChannelOwnership::state(quint16 channelId) const {
    return entryFor(channelId).state;
}

bool ChannelOwnership::isOwnedByLocal(quint16 channelId) const {
    const ChannelEntry& entry = entryFor(channelId);
    return entry.state == OwnershipState::OwnedLocal &&
            entry.ownerPeerId == m_selfPeerId;
}

bool ChannelOwnership::isOwnedByRemote(quint16 channelId) const {
    const ChannelEntry& entry = entryFor(channelId);
    return entry.state == OwnershipState::OwnedRemote &&
            entry.ownerPeerId != m_selfPeerId;
}

bool ChannelOwnership::isOwnedByPeer(quint16 channelId, quint8 peerId) const {
    const ChannelEntry& entry = entryFor(channelId);
    return entry.ownerPeerId == peerId &&
            (entry.state == OwnershipState::OwnedLocal ||
             entry.state == OwnershipState::OwnedRemote);
}

bool ChannelOwnership::canClaim(quint16 channelId) const {
    return entryFor(channelId).state == OwnershipState::Unowned;
}

quint8 ChannelOwnership::ownerPeerId(quint16 channelId) const {
    return entryFor(channelId).ownerPeerId;
}

ChannelOwnership::ChannelEntry& ChannelOwnership::entryFor(quint16 channelId) {
    if (channelId >= static_cast<quint16>(m_channels.size())) {
        // Return a static fallback for invalid IDs
        static ChannelEntry s_fallback;
        s_fallback.state = OwnershipState::Unowned;
        s_fallback.ownerPeerId = 0;
        return s_fallback;
    }
    return m_channels[channelId];
}

const ChannelOwnership::ChannelEntry& ChannelOwnership::entryFor(quint16 channelId) const {
    if (channelId >= static_cast<quint16>(m_channels.size())) {
        static const ChannelEntry s_fallback{OwnershipState::Unowned, 0};
        return s_fallback;
    }
    return m_channels[channelId];
}

void ChannelOwnership::setState(quint16 channelId, OwnershipState newState, quint8 peerId) {
    if (channelId >= static_cast<quint16>(m_channels.size())) {
        return;
    }
    if (m_channels[channelId].state != newState ||
            m_channels[channelId].ownerPeerId != peerId) {
        m_channels[channelId].state = newState;
        m_channels[channelId].ownerPeerId = peerId;
        emit ownershipChanged(channelId, newState);
    }
}

#include "moc_channelownership.cpp"
