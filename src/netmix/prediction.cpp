#include "netmix/prediction.h"

#include "netmix/controlallowlist.h"

NetmixInputFrame HoldLastPrediction::predict(quint32 tick, const InputBuffer& buffer) {
    if (buffer.hasRemote(tick) && buffer.isRemoteConfirmed(tick)) {
        return buffer.remoteFrameAt(tick);
    }

    bool found = false;
    NetmixInputFrame lastConfirmed;
    for (int i = 1; i <= InputBuffer::kDefaultCapacity; ++i) {
        quint32 searchTick = tick - static_cast<quint32>(i);
        if (buffer.hasRemote(searchTick) && buffer.isRemoteConfirmed(searchTick)) {
            lastConfirmed = buffer.remoteFrameAt(searchTick);
            found = true;
            break;
        }
    }

    if (!found) {
        NetmixInputFrame empty;
        empty.baseTick = tick;
        return empty;
    }

    NetmixInputFrame predicted;
    predicted.baseTick = tick;
    for (const auto& ev : lastConfirmed.events) {
        auto kind = ControlAllowlist::kindForWireId(ev.wireId);
        if (kind.has_value() && *kind == ControlKind::Continuous) {
            predicted.events.append(ev);
        }
    }
    return predicted;
}
