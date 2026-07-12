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

## 2026-07-12: Phase Merge 1.3 (Task 1.3.5)

| Decision | Value |
|---|---|
| Merge commit | `7bffbeb4c1` on `feat/rollback-network-mixing` |
| Strategy | `--no-ff` (branch history preserved) |
| Source | `release/1.3` (11 commits: tasks 1.3.1–1.3.4 plus merge commits) |
| Pre-merge gate | `ctest -R Netmix`: 85/85 pass |
| Post-merge gate | `ctest -R Netmix`: 85/85 pass |
| Trunk tree after merge | Identical to `release/1.3` (diff empty) |
| Deferred: ClockSync UDP port-sharing | `ClockSync::start` fails on macOS due to `SO_REUSEPORT` not exposed by Qt. Creation commented out in `NetmixSessionManager::onTcpConnected`. Unresolved — fix in Phase 1.4/1.5. See memory.md 2026-07-12 line 132. |
| Phase 1.4 next | Begin on `task-1.4.1` off `release/1.4` (created from trunk after this merge) |

## 2026-07-12: InputBuffer Ring Buffer (Task 1.4.1)

| Decision | Value |
|---|---|
| Capacity | 256 ticks (const `InputBuffer::kDefaultCapacity`). Resizable via `setCapacity`, which triggers `clear`. |
| Indexing | `tick % capacity` modular arithmetic. Window `[oldestTick, newestTick]` maintained. Slots outside window evicted on window advance. |
| Divergence detection | `eventsDiffer()` compares predicted vs confirmed events bidirectionally: same wireId different value, extra wireId in confirmed, missing wireId in predicted — all trigger divergence. Uses `qFuzzyCompare` for double comparison. |
| Cached divergence | `m_cachedFirstDivergentTick` updated on `insertRemoteConfirmed` when divergence detected. `firstDivergentTick()` returns cached if set, otherwise scans all slots. Reset on `clear()` and `advanceWindow()` when empty. |
| Tick comparison | Modular arithmetic via `tickLt`/`tickLe`/`tickGt`/`tickGe` using `(qint32)(a - b)` (window << 2^31, safe for capacity 256). |
| No thread safety | InputBuffer lives in Qt thread (same as Capture/Applier). No locks. Not accessed from audio callback. |
| Event limit | `kMaxEventsPerTick = 32` (duplicated from `InputFramePacker`). Events truncated to this count on insert. |

## 2026-07-12: RollbackEngine (Task 1.4.3)

| Decision | Value |
|---|---|
| Snapshot storage | Ring buffer of `m_windowSize` entries. Each entry is `QVector<double>` sized to `m_proxies.size()` (number of allowlist entries). Indexed by `tick % m_windowSize`. |
| Window | Default 8 ticks, max 30 (from `kDefaultWindow`/`kMaxWindow`). Set via `setWindowSize()`, which clears existing snapshots. |
| Snapshot proxies | RollbackEngine creates its own `ControlProxy` instances (one per allowlist entry) for reading CO values at snapshot time. Owns them via Qt parent chain (`this`). |
| Rollback trigger | `onTick()` takes snapshot then checks `InputBuffer::firstDivergentTick()`. If divergence within window, restores snapshot at T-1, re-applies confirmed remote + local from T..now, predicts unconfirmed tail via `PredictionStrategy`. |
| Window exceeded | Divergent tick older than `currentTick - windowSize`: logs once (`qWarning` with `[Netmix]` prefix), counts, emits `windowExceeded(tick)`, applies the confirmed frame directly (forward correction only, no rollback). |
| Re-simulation apply | All value changes during re-simulation go through `ControlApplier::apply()`, which shares proxy instances with `ControlCapture` — echo suppression automatically filters rollback inputs from capture output. |
| Apply order per tick | Confirmed remote first, then local (local overrides remote for same wireId within a tick). Then prediction for unconfirmed ticks. |
| Snapshot recovery | If exact snapshot for T-1 isn't found (ring wrap), scans all slots for the closest snapshot at or before T-1. If none found — logs warning and skips rollback. |
| No clock dependency | RollbackEngine does NOT own or use SessionClock. Ticks come from the caller (session manager). Snapshotring is self-contained. |
| Tear-down | `qDeleteAll(m_proxies)` in destructor (Qt parented, but explicit for test cleanliness). |
| Stats | `rollbackCount()` and `windowExceededCount()` exposed for monitoring. |
| Signals | `rollbackPerformed(quint32 fromTick, quint32 toTick)` and `windowExceeded(quint32 tick)` for session manager / UI monitoring. |

## 2026-07-12: Interpolation Reconciliation (Task 1.4.4)

| Decision | Value |
|---|---|
| Ramp constants | `kRampScale=4.0`, `kMaxRampTicks=4`. Ramp duration = `qBound(1, int(|target-current| * 4.0), 4)` ticks. A 0.25 correction ramps 1 tick (~4ms at 240Hz), a 1.0 correction ramps 4 ticks (~17ms). |
| `advanceTick` in `onTick` | `m_pApplier->advanceTick()` called at `onTick` entry, before `takeSnapshot`. Ramps progress one tick per engine tick. |
| Re-simulation ramp | Confirmed remote Continuous events during re-sim go through `applyRamped` instead of `apply`. Discrete/Seek still use `apply` (snap). |
| Corrected wireId set | `QSet<quint16> rampedWireIds` tracked during re-sim. Predicted events for wireIds in this set are skipped. Local events on ramped wireIds `apply()` (snap cancelling the ramp) and remove from the set. |
| Ramp supersession | Newer confirmed correction on same wireId calls `applyRamped` again (ControlApplier supersedes in-flight ramp). Local input on same wireId calls `apply` (snap, ramp cancelled). |
| Window-exceeded path | No ramp — forward correction still uses `apply()` (snap). Acceptable — late-arriving packets outside window are rare. |
| Existing tests | Updated to expect ramp baseline (0.0) after rollback instead of immediate snap value. New ramp-specific tests added. |

## 2026-07-12: Optional 64th-Note Quantizer (Task 1.4.5)

| Decision | Value |
|---|---|
| 64th-grid formula | `tpg = tickRate * 60.0 / (bpm * 16.0)` — one 64th note = 1/16 quarter note. Snapped = `qRound(tick / tpg) * tpg + 0.5` truncated to `quint32`. |
| Enabled by `[Netmix],quantize` CO | CO created in ctor (default 0 = off). `valueChanged` → `setEnabled(value > 0.5)` in `onTcpConnected`. |
| BPM source | `[InternalClock], bpm` via `ControlProxy` created alongside quantizer in `onTcpConnected`. Read per-tick, safe (no lock, just `get()`). |
| BPM guard | `bpm <= 0.0` → passthrough. Safe against uninitialized clock. |
| Disabled passthrough | `m_enabled == false` → byte-identical `return tick`. |
| Symmetry | Both peers compute snap independently from same BPM. No wire format change. |
| Snapped tick in send path | `onTickAdvanced` snaps tick before `finishTick`. |
| Snapped tick in recv path | `onInputFrameReceived` snaps `baseTick` (consumed when InputBuffer is wired later). |
| Continuous values | Snap only touches tick, never value (no value parameter in `snap()`). |
| Audio-callback purity | Quantizer runs only in Qt thread (same as session manager). No locks, no allocations. |

## 2026-07-12: Phase Merge 1.4 (Task 1.4.6)

| Decision | Value |
|---|---|
| Merge commit | `45a98712d6` on `feat/rollback-network-mixing` |
| Strategy | `--no-ff` (branch history preserved) |
| Source | `release/1.4` (15 commits: tasks 1.4.1–1.4.5 plus merge/docs commits) |
| Pre-merge gate | `ctest -R Netmix`: 123/123 pass |
| Post-merge gate | `ctest -R Netmix`: 123/123 pass |
| Trunk tree after merge | Identical to `release/1.4` (diff empty) |
| Deferred re: ClockSync | ClockSync UDP port-sharing still not resolved. `ClockSync::start` fails on macOS (`SO_REUSEPORT` not exposed by Qt). Creation remains commented out in `NetmixSessionManager::onTcpConnected`. Re-deferred to phase 1.5. See memory.md line 132. |
| Phase 1.5 next | Begin on `task-1.5.1` off `release/1.5` (created from trunk after this merge) |

## 2026-07-12: Channel Ownership & Locks (Phase 1.5)

| Decision | Value |
|---|---|
| Protocol version | Bumped to 4. `NetmixHello` and `NetmixHelloAck` carry `QVector<quint16> preassignedChannels`. Old v3 decoders reject v4 payloads via `atEnd()` check (shorter payload). |
| Pre-assignment race | Lower peerId wins. If both peers pre-assign the same channel, the handshake's lower peerId (host=0) gets it. Resolved in `ChannelOwnership::resolvePreAssignment()` called after both local+remote pre-assignment lists are known. |
| Simultaneous claim | Both peers claim an open channel before seeing each other's claim. Lower peerId wins: lower peer sends Deny to higher, higher peer auto-concedes (auto-denies locally) and sends Grant to lower. Both converge to same owner. |
| Channel ID map | wireId → channelId via `ControlAllowlist::channelForWireId()`. Per-deck wireIds (1-72) map to channel (1-4) using `((wireId-1) % 4) + 1`. Crossfader (wireId 73) → channelId 0. Invalid wireIds → nullopt. |
| Capture enforcement | `ControlCapture::onProxyValueChange` checks `isOwnedByLocal()` via `ChannelOwnership`. Non-owned channel events are dropped before `emit captured()`. Ownership check runs before echo suppression (`m_muted` check). |
| Applier enforcement | `ControlApplier::apply()`/`applyRamped()` checks `isOwnedByRemote()` via `ChannelOwnership` when ownership filter is enabled. Filter enabled only for incoming remote input frames (`onInputFrameReceived`). Disabled for re-simulation. |
| Ownership messages | Claim/Grant/Deny/Release routed through `TcpSession::messageReceived` → `NetmixSessionManager::onTcpMessageReceived` → `ChannelOwnership` methods. Auto-response: unowned channel receiving claim → Grant; owned channel receiving claim → Deny. |
| Disconnect cleanup | `ChannelOwnership::autoReleaseAll()` called on `leaveSession()` and `onTcpDisconnected()`. All channels reset to Unowned. |
| helloComplete signal | Emitted by `TcpSession` after processing `Hello` or `HelloAck`, carrying the remote peer's pre-assigned channels. Session manager feeds both pre-assignment lists to `ChannelOwnership::resolvePreAssignment()`. Signal connected before handshake starts (in `hostSession`/`joinSession`). |
| ChannelOwnership channels | 5 slots: channelId 0 (crossfader), 1-4 (decks). Fixed-size vector, bounds-checked. |

## 2026-07-12: Prediction Hold-Last (Task 1.4.2)

| Decision | Value |
|---|---|
| Strategy interface | `PredictionStrategy` abstract base class with virtual `predict(tick, buffer)`. `HoldLastPrediction` is the initial implementation. Enables swapping to velocity-extrapolation or NN prediction without touching callers. |
| Hold-last algorithm | For ticks with a confirmed remote frame: return it as-is. For ticks without: search backward up to `kDefaultCapacity` ticks, find the most recent confirmed frame, copy only `ControlKind::Continuous` events (volume/pregain/filter/crossfader). Discrete/seek events NOT carried forward — they fire once and don't hold. |
| InputBuffer API extension | `isRemoteConfirmed(tick)` added to `InputBuffer` (was missing from 1.4.1). Returns true only if slot is occupied, tick matches, AND `confirmed` flag is set. Required by prediction to distinguish confirmed data from previously-predicted frames during re-simulation. Without this, re-prediction after a rollback would return stale predicted frames with outdated last-known values. |

## 2026-07-12: TrackTransfer Chunked File Transfer (Task 1.6.2)

| Decision | Value |
|---|---|
| TrackTransfer ownership | Takes `TcpSession*` and `TrackCache*` as non-owning pointers. Parented to session manager (or test). |
| Signal routing | Connects to `TcpSession::messageReceived` in constructor. All track message types routed internally via switch. |
| Partial file naming | `<hash>.<ext>.partial` inside cache dir. Extension derived from mime type via `mimeToExt()`. |
| Verify-then-rename | After TrackComplete, receiver computes SHA-256 of .partial. Pass → rename to `<hash>.<ext>`, insert into TrackCache, send TrackReady. Fail → delete .partial, reset IncomingTransfer (keep map entry), send TrackAccept{hash, 0} to restart. |
| Batch sender | Sends up to 4 chunks (64 KiB each) per `sendNextBatch()`. After each batch, if more data remains, schedules `QTimer::singleShot(0)` to yield event loop for pending control messages. |
| Chunk read size | `kChunkSize = 65536` (64 KiB). |
| Max chunks per batch | `kMaxChunksPerBatch = 4`. Bounded: at most 4 chunks pending at once. |
| Resume | Outgoing: sender seeks to `haveBytes` from TrackAccept. Incoming: receiver opens .partial in Append mode, sets `bytesReceived = haveBytes`. Both sides agree on `haveBytes` as the first byte not yet received. |
| Hash format | Protocol uses `QByteArray` (32 raw bytes). Cache uses `QString` (64 hex chars). Convert via `toHex()`/`fromHex()`. |
| Cache insertion post-transfer | After verify and rename, calls `m_pCache->insert(finalPath)` which detects existing file (skips copy), adds entry to index. |
| CancelAll | Closes all file handles, removes partial files, clears both transfer maps. |
| No protocol changes | All six track message structs (TrackOffer, TrackAccept, TrackChunk, TrackComplete, TrackReady) existed from Task 1.1.2 with full serialization. |

## 2026-07-12: TrackCache Directory & Index (Task 1.6.1)

| Decision | Value |
|---|---|
| Cache dir name | `netmix_cache/` under settings dir |
| Index file | `index.json`, version 1, JSON format: `{version, entries: {hash: {originalFilename, size, sourcePeer, addedTimestamp, verified}}}` |
| Rebuild | Scan dir for files matching `64hexchars.ext`, trust filename as content hash, set verified=true |
| Path safety | `isPathSafe` checks `QDir::cleanPath(candidate).startsWith(m_cacheDir.canonicalPath())`. Applied to all cache output paths. Source file paths in `insert()` not checked (external input, can be anywhere). |
| File naming | `<sha256>.<ext>` inside cache dir |
| SHA-256 | `QCryptographicHash::Sha256` |
| Thread | Qt thread only, no locks |
