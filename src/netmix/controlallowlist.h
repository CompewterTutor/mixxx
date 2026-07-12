#pragma once

#include <QHash>
#include <QVector>
#include <optional>

#include "control/controlproxy.h"

enum class ControlKind : quint8 {
    Continuous = 0,
    Discrete = 1,
    Seek = 2,
};

struct AllowlistEntry {
    ConfigKey key;
    quint16 wireId;
    ControlKind kind;
};

class ControlAllowlist {
  public:
    static const QVector<AllowlistEntry>& entries();
    static std::optional<quint16> wireIdForKey(const ConfigKey& key);
    static std::optional<ConfigKey> keyForWireId(quint16 wireId);
    static std::optional<ControlKind> kindForWireId(quint16 wireId);

  private:
    static const QVector<AllowlistEntry>& buildTable();
    static QHash<ConfigKey, quint16> s_keyToWire;
    static QHash<quint16, AllowlistEntry> s_wireToEntry;
    static QVector<AllowlistEntry> s_entries;
    static bool s_built;
};
