# Netmix — Cross-Cutting Decisions

## 2026-07-12: Wire Protocol (Task 1.1.2)

| Decision | Value |
|---|---|
| Wire magic | `0x584D4E` (big-endian ASCII `"NMX\0"`) |
| Wire version | `1` (uint16) |
| Header layout | 12 bytes: magic(u32), version(u16), type(u16), length(u32) — all via QDataStream LittleEndian |
| Byte order | Little-endian (QDataStream::LittleEndian) |
| QDataStream version | `Qt_6_0` — pinned for wire stability across Qt minor versions |
| Payload encoding | QDataStream built-in serialization for QString, QByteArray, QVector, quint16, quint32, quint64, double |
| Header encoding | Fields written individually via QDataStream (not as a raw C struct) to avoid alignment/padding differences between compilers/architectures |
| Message types | 15 types (0–14): Hello, HelloAck, Ping, Pong, InputFrame, OwnershipClaim, OwnershipGrant, OwnershipDeny, OwnershipRelease, TrackOffer, TrackAccept, TrackChunk, TrackComplete, TrackReady, Bye |
| encodeMessage | Writes header then payload bytes; returns QByteArray |
| decodeMessage | Validates magic/version/length, deserializes payload by type, checks atEnd(); returns nullopt on any error |
