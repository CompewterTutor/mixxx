# Netmix — Cross-Cutting Decisions

## 2026-07-12: SessionClock Tick Math (Task 1.1.3)

| Decision | Value |
|---|---|
| Tick rate | 240 Hz (constant `SessionClock::kTickRate`) |
| Tick computation | `(totalFrames * kTickRate) / sampleRate` — integer floor from accumulated totalFrames, recomputed every `onFramesProcessed`. No float, no per-buffer remainder accumulation. |
| Drift | Zero by construction — every tick is a fresh floor of the rational totalFrames*kTickRate/sampleRate. |
| Sample rate passed per call | `onFramesProcessed(int frames, int sampleRate)`. Rate may differ between calls; tick math uses the current call's rate for the entire accumulated totalFrames. Monotonicity enforced via `newTick > m_currentTick` guard. |
| `agreedTick()` | `(quint32)((qint64)currentTick + offset)` — wraps at 2^32 (~207 days at 240 Hz), two's complement deltas work naturally. |
| Signal emission | `tickAdvanced(quint32 tick)` emitted from `onFramesProcessed` when tick increments. Caller must ensure this is safe (Qt thread, or queued connection from engine thread). Flagged for review in later threading audit. |

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
