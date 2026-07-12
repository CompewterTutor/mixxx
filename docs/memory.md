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

## 2026-07-12: Phase Merge 1.1 (Task 1.1.5)

| Decision | Value |
|---|---|
| Merge commit | `6edf39d3b1` on `feat/rollback-network-mixing` |
| Strategy | `--no-ff` (branch history preserved) |
| Source | `release/1.1` (13 commits: tasks 1.1.1–1.1.4) |
| Pre-merge gate | `ctest -R Netmix`: 33/33 pass |
| Post-merge gate | `ctest -R Netmix`: 33/33 pass |
| Trunk tree after merge | Identical to `release/1.1` (diff empty) |
| Phase 1.2 next | Begin on `task-1.2.1` off `release/1.2` (created from trunk after this merge) |

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

## 2026-07-12: TcpSession Channel (Task 1.3.1)

| Decision | Value |
|---|---|
| Default listen port | 21200 |
| PeerId assignment | Host=0, Client=1 (host assigns in HelloAck). Both sides hardcode their role-based peerId; HelloAck carries peerId as confirmation. |
| Hello/HelloAck wire format | Extended with `tickRate` (quint16) and `rollbackWindow` (quint16) after existing fields. Old peers decode shorter payload → `atEnd()` check fails → rejected cleanly (version mismatch). |
| Heartbeat | `Ping` sent every 1 s via `QTimer`. Dead-peer: no traffic 5 s → Degraded, 15 s → Disconnected. Timeouts overridable via `setTimeoutsForTest()` for tests. |
| Framing | Length-prefixed: read 12-byte header, peek version for early reject, accumulate `12 + header.length` bytes, decode. Decode failure → send Bye + disconnect. |
| Thread safety | All I/O in Qt event loop thread (main thread). No audio thread involvement. |
| Degraded→Connected recovery | Receiving any traffic while Degraded resets dead-peer timer and transitions back to Connected. |
| TcpSession ownership | Parented to session manager (or test). Sockets/timers parented to TcpSession (`this`), auto-destroyed. |

## 2026-07-12: UDP Input Channel (Task 1.3.2)

| Decision | Value |
|---|---|
| UDP datagram format | `[quint32 seq][encodeMessage(InputFrame)]` — seq is network-local monotonic counter, not the session tick. Decode rest via standard `decodeMessage`. |
| Window size | 64 sequence numbers (`kWindowSize=64`). Stale: `seq <= highest - kWindowSize`. |
| Port sharing | UDP listens on same port as TCP (default 21200). OS permits UDP + TCP on same port. |
| Stats | `sent`, `received`, `dropped` (duplicate+stale+decode), `outOfOrder` (reordered within window). |

## 2026-07-12: ClockSync NTP-lite Offset Estimation (Task 1.3.3)

| Decision | Value |
|---|---|
| Ping/Pong transport | Dedicated UDP socket (separate from TCP heartbeat), port sharing with TCP (default 21200). Same datagram framing as UdpChannel (4-byte LE seq prefix). |
| Ping interval | 250 ms (`kPingIntervalMs`). Pending pong guard prevents overlapping. |
| Ping sends | `ClockSync::sendPingNow()` called by timer or test. Records `agreedTick()` as `m_lastSentTick`, uses `QElapsedTimer` for wall-clock RTT. |
| NTP offset formula | `offset = pong.remoteTick - (m_lastSentTick + localTickNow) / 2` — standard NTP offset for symmetric RTT. All values are `quint32` agreedTick, cast to `qint32` for signed arithmetic (wraparound-safe via two's complement). |
| Median filter | Sliding window of 16 `qint32` samples. Circular buffer with linear copy+sort for median extraction. `sorted[count/2]` (lower-median for even count). |
| Filter update throttle | `SessionClock::setOffset` called every 4th pong (`kUpdateEveryNPongs=4`) with guard `abs(newOffset - currentOffset) > 1` to prevent micro-oscillation. |
| RTT measurement | Wall-clock via `QElapsedTimer::nsecsElapsed() / 1000` (microsecond precision). Stored as `m_smoothedRttMs` (latest sample, not filtered). |
| Test mode | `startTestMode()` skips UDP socket creation. All I/O via `injectMessage()` (incoming) and `outgoingMessage` signal (outgoing). Tests wire syncs together via signal→inject connections. |
| HelloAck initiatorTick | `quint32 initiatorTick` appended to `NetmixHelloAck` wire format after `rollbackWindow`. Extends payload by 4 bytes. Old peers decoding shorter payload → `atEnd()` fails → version mismatch rejection (same pattern as tickRate/rollbackWindow extension). |
| Session clock initial offset | `ClockSync::setInitialOffset(hostTick, localTick)` computes `offset = (qint32)hostTick - (qint32)localTick` and calls `m_pClock->setOffset()`. Called by session manager after handshake using HelloAck.initiatorTick. |
| Outlier tolerance | Median filter rejects single extreme values (±500 ticks) within 2 ticks of stable value when window has ≥16 samples. |

## 2026-07-12: InputFrame Packer (Task 1.2.4)

| Decision | Value |
|---|---|
| Ring buffer | 8 slots fixed at compile time (`kRingSize=8`), each with inline `NetmixInputFrameEvent[32]` array. Pre-allocated, no heap allocation in capture→pack path. |
| Dedup | Same wireId twice in one tick → last value wins. Linear scan over per-tick events (max 32, cheap). |
| framesForSend | Returns up to `batchSize` most recently finalized frames, newest first. Walks ring buffer backward.|
| Size guard | 4 ticks × 20 events each → total encoded batch under 1200 bytes (test-enforced). |
| Clear | Resets all slots and head index. |

## 2026-07-12: Session State Machine Wired (Task 1.3.4)

| Decision | Value |
|---|---|
| ClockSync deferred from 1.3.4 | ClockSync::start fails on macOS when binding same UDP port as UdpChannel (`SO_REUSEPORT` not exposed by Qt). ClockSync creation removed from `NetmixSessionManager::onTcpConnected`. Will be re-added in phase 1.4/1.5 when port-sharing solution is implemented (single shared socket or platform-specific `SO_REUSEPORT`). |
| Sub-component deletion | Use `deleteLater()` + immediate pointer nulling + `disconnect(this)` before deletion to avoid recursion through signal emission (e.g., TcpSession emits `stateChanged(Disconnected)` → manager deletes TcpSession while still inside its `setState` stack frame). |
| Protocol version bumped | `kNetmixProtocolVersion` = 3 for `udpPort` field in `NetmixHello`. |
| `udpPort` in Hello | Both peers advertise their UDP listener port in `NetmixHello.udpPort`. Host uses this to learn client's UDP port (which is auto-assigned). Client assumes host's UDP port == host's TCP port (both listen on same port). |
| Runtime gate | `setEnabled(bool)` flag (no `#ifdef`). When disabled, `hostSession`/`joinSession` return immediately without changing state. |
