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

## 2026-07-12: NetmixSessionManager Service (Task 1.1.4)

| Decision | Value |
|---|---|
| `[Netmix],status` CO | Read-only, set via `forceSet` (not `set`) so state transitions always produce a `valueChanged` even if same double value is reused. |
| SessionState enum ↔ CO | Enum values match CO double directly: 0=Idle, 1=Connecting, 2=Connected, 3=Degraded. No mapping table. |
| Always compiled | `NetmixSessionManager` has no `#ifdef` guard — always compiled, like `PlayerManager`/`RecordingManager` (unlike `BroadcastManager` which is `__BROADCAST__`-gated). |
| Teardown order | `NetmixSessionManager` destroyed in `CoreServices::finalize()` after `BroadcastManager`, before `EngineMixer`. |
| CO lifetime | `ControlObject* m_pStatusCO` created with `new` in ctor, `delete`d in dtor (same as `BroadcastManager`). Registered in `ControlDoublePrivate` singleton map; deletion unregisters it. |

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

## 2026-07-12: Control Allowlist (Task 1.2.1)

| Decision | Value |
|---|---|
| Wire ID assignment | Flat sequential IDs per-deck: play(1-4), cue_default(5-8), start(9-12), end(13-16), playposition(17-20), rate(21-24), volume(25-28), pregain(29-32), hotcue_1..8_activate(33-64), EQ superknob(65-68), QuickEffect superknob(69-72); crossfader(73). 73 entries total. |
| ControlKind | Continuous for volume/rate/pregain/filter-superknobs/crossfader; Discrete for play/cue_default/start/end/hotcue; Seek for playposition. |
| Lookup maps | `QHash<ConfigKey, quint16>` key→wireId, `QHash<quint16, AllowlistEntry>` wireId→entry. Built once on first access. |
| Decks | 4 decks expanded at build-time from `[ChannelN]` pattern. |

## 2026-07-12: ControlCapture (Task 1.2.2)

| Decision | Value |
|---|---|
| Echo suppression | Uses `ControlProxy::connectValueChanged` built-in `pSetter != this` check. Capture creates proxies; Applier reuses same proxy instances for `set()`, so `pSetter == proxy` → capture's signal handler drops it. No separate sentinel object needed. |
| Proxy ownership | `ControlProxy` objects parented to `ControlCapture` (`this` as parent). Deleted via `qDeleteAll` in `stop()`, or by Qt parent chain in destructor. |
| Tick stamping | Each captured event stamped with `SessionClock::agreedTick()` at the moment the signal fires. |
| Connection type | `Qt::DirectConnection` — capture and applier both live in Qt thread, never audio thread. |
| Capture lifetime | `start(clock)`→`stop()` cycle. Proxies disconnected and destroyed on stop. Restart works cleanly. |

## 2026-07-12: ControlApplier (Task 1.2.3)

| Decision | Value |
|---|---|
| Proxy reference | Applier holds raw pointers to Capture's proxies. Valid only while Capture is started. |
| wireId lookup | `QHash<quint16, int>` mapping wireId→proxy index, rebuilt on `setProxies`. |
| Ramp interpolation | Linear: `current = target + (start - target) * (remaining/total)`. Pre-allocated vector sized for continuous controls. |
| Ramp supersession | A new `apply()` or `applyRamped()` on the same wireId cancels the in-flight ramp. Previous ramp entry marked `remainingTicks=0` (in `apply`) or superseded start value (in `applyRamped`). |
| Seek routing | `ControlKind::Seek` applies directly (`proxy->set()`) even if `applyRamped` is called — no ramp for seek controls. |

## 2026-07-12: InputFrame Packer (Task 1.2.4)

| Decision | Value |
|---|---|
| Ring buffer | 8 slots fixed at compile time (`kRingSize=8`), each with inline `NetmixInputFrameEvent[32]` array. Pre-allocated, no heap allocation in capture→pack path. |
| Dedup | Same wireId twice in one tick → last value wins. Linear scan over per-tick events (max 32, cheap). |
| framesForSend | Returns up to `batchSize` most recently finalized frames, newest first. Walks ring buffer backward.|
| Size guard | 4 ticks × 20 events each → total encoded batch under 1200 bytes (test-enforced). |
| Clear | Resets all slots and head index. |
