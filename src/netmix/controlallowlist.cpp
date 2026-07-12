#include "netmix/controlallowlist.h"

namespace {

constexpr int kNumDecks = 4;
constexpr int kNumHotcues = 8;

// Wire ID base offsets per control type (per deck).
// Each per-deck control gets 4 consecutive IDs (one per deck).
// Layout (wire IDs 1-73):
//   1-4:   play
//   5-8:   cue_default
//   9-12:  start
//   13-16: end
//   17-20: playposition
//   21-24: rate
//   25-28: volume
//   29-32: pregain
//   33-64: hotcue_1..8_activate (4 each)
//   65-68: EqualizerRack1_[ChannelN] superknob
//   69-72: QuickEffectRack1_[ChannelN] superknob
//   73:    [Master] crossfader

constexpr quint16 kBasePlay = 1;
constexpr quint16 kBaseCueDefault = 5;
constexpr quint16 kBaseStart = 9;
constexpr quint16 kBaseEnd = 13;
constexpr quint16 kBasePlaypos = 17;
constexpr quint16 kBaseRate = 21;
constexpr quint16 kBaseVolume = 25;
constexpr quint16 kBasePregain = 29;
constexpr quint16 kBaseHotcue = 33;
constexpr quint16 kBaseEqSuperknob = 65;
constexpr quint16 kBaseQuickEffectSuperknob = 69;
constexpr quint16 kGlobalCrossfader = 73;
constexpr int kDeckStride = 4;

void addPlayEntries(QVector<AllowlistEntry>& entries) {
    for (int d = 0; d < kNumDecks; ++d) {
        entries.push_back({{QStringLiteral("Channel%1").arg(d + 1),
                                   QStringLiteral("play")},
                static_cast<quint16>(kBasePlay + d), ControlKind::Discrete});
    }
}

void addCueDefaultEntries(QVector<AllowlistEntry>& entries) {
    for (int d = 0; d < kNumDecks; ++d) {
        entries.push_back({{QStringLiteral("Channel%1").arg(d + 1),
                                   QStringLiteral("cue_default")},
                static_cast<quint16>(kBaseCueDefault + d), ControlKind::Discrete});
    }
}

void addStartEntries(QVector<AllowlistEntry>& entries) {
    for (int d = 0; d < kNumDecks; ++d) {
        entries.push_back({{QStringLiteral("Channel%1").arg(d + 1),
                                   QStringLiteral("start")},
                static_cast<quint16>(kBaseStart + d), ControlKind::Discrete});
    }
}

void addEndEntries(QVector<AllowlistEntry>& entries) {
    for (int d = 0; d < kNumDecks; ++d) {
        entries.push_back({{QStringLiteral("Channel%1").arg(d + 1),
                                   QStringLiteral("end")},
                static_cast<quint16>(kBaseEnd + d), ControlKind::Discrete});
    }
}

void addPlayposEntries(QVector<AllowlistEntry>& entries) {
    for (int d = 0; d < kNumDecks; ++d) {
        entries.push_back({{QStringLiteral("Channel%1").arg(d + 1),
                                   QStringLiteral("playposition")},
                static_cast<quint16>(kBasePlaypos + d), ControlKind::Seek});
    }
}

void addRateEntries(QVector<AllowlistEntry>& entries) {
    for (int d = 0; d < kNumDecks; ++d) {
        entries.push_back({{QStringLiteral("Channel%1").arg(d + 1),
                                   QStringLiteral("rate")},
                static_cast<quint16>(kBaseRate + d), ControlKind::Continuous});
    }
}

void addVolumeEntries(QVector<AllowlistEntry>& entries) {
    for (int d = 0; d < kNumDecks; ++d) {
        entries.push_back({{QStringLiteral("Channel%1").arg(d + 1),
                                   QStringLiteral("volume")},
                static_cast<quint16>(kBaseVolume + d), ControlKind::Continuous});
    }
}

void addPregainEntries(QVector<AllowlistEntry>& entries) {
    for (int d = 0; d < kNumDecks; ++d) {
        entries.push_back({{QStringLiteral("Channel%1").arg(d + 1),
                                   QStringLiteral("pregain")},
                static_cast<quint16>(kBasePregain + d), ControlKind::Continuous});
    }
}

void addHotcueEntries(QVector<AllowlistEntry>& entries) {
    for (int h = 0; h < kNumHotcues; ++h) {
        const QString cueName =
                QStringLiteral("hotcue_%1_activate").arg(h + 1);
        for (int d = 0; d < kNumDecks; ++d) {
            entries.push_back(
                    {{QStringLiteral("Channel%1").arg(d + 1), cueName},
                            static_cast<quint16>(kBaseHotcue + h * kDeckStride + d),
                            ControlKind::Discrete});
        }
    }
}

void addEqSuperknobEntries(QVector<AllowlistEntry>& entries) {
    for (int d = 0; d < kNumDecks; ++d) {
        entries.push_back(
                {{QStringLiteral("EqualizerRack1_[Channel%1]").arg(d + 1),
                         QStringLiteral("superknob")},
                        static_cast<quint16>(kBaseEqSuperknob + d),
                        ControlKind::Continuous});
    }
}

void addQuickEffectSuperknobEntries(QVector<AllowlistEntry>& entries) {
    for (int d = 0; d < kNumDecks; ++d) {
        entries.push_back(
                {{QStringLiteral("QuickEffectRack1_[Channel%1]").arg(d + 1),
                         QStringLiteral("superknob")},
                        static_cast<quint16>(kBaseQuickEffectSuperknob + d),
                        ControlKind::Continuous});
    }
}

void addGlobalEntries(QVector<AllowlistEntry>& entries) {
    entries.push_back({{QStringLiteral("Master"), QStringLiteral("crossfader")},
            kGlobalCrossfader, ControlKind::Continuous});
}

} // anonymous namespace

QHash<ConfigKey, quint16> ControlAllowlist::s_keyToWire;
QHash<quint16, AllowlistEntry> ControlAllowlist::s_wireToEntry;
QVector<AllowlistEntry> ControlAllowlist::s_entries;
bool ControlAllowlist::s_built = false;

const QVector<AllowlistEntry>& ControlAllowlist::buildTable() {
    if (s_built) {
        return s_entries;
    }

    addPlayEntries(s_entries);
    addCueDefaultEntries(s_entries);
    addStartEntries(s_entries);
    addEndEntries(s_entries);
    addPlayposEntries(s_entries);
    addRateEntries(s_entries);
    addVolumeEntries(s_entries);
    addPregainEntries(s_entries);
    addHotcueEntries(s_entries);
    addEqSuperknobEntries(s_entries);
    addQuickEffectSuperknobEntries(s_entries);
    addGlobalEntries(s_entries);

    for (const auto& entry : s_entries) {
        s_keyToWire.insert(entry.key, entry.wireId);
        s_wireToEntry.insert(entry.wireId, entry);
    }

    s_built = true;
    return s_entries;
}

const QVector<AllowlistEntry>& ControlAllowlist::entries() {
    return buildTable();
}

std::optional<quint16> ControlAllowlist::wireIdForKey(const ConfigKey& key) {
    buildTable();
    auto it = s_keyToWire.constFind(key);
    if (it != s_keyToWire.constEnd()) {
        return it.value();
    }
    return std::nullopt;
}

std::optional<ConfigKey> ControlAllowlist::keyForWireId(quint16 wireId) {
    buildTable();
    auto it = s_wireToEntry.constFind(wireId);
    if (it != s_wireToEntry.constEnd()) {
        return it->key;
    }
    return std::nullopt;
}

std::optional<ControlKind> ControlAllowlist::kindForWireId(quint16 wireId) {
    buildTable();
    auto it = s_wireToEntry.constFind(wireId);
    if (it != s_wireToEntry.constEnd()) {
        return it->kind;
    }
    return std::nullopt;
}
