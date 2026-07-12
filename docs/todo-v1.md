# Netmix v1 — Core Rollback Sync

Milestone goal: two Mixxx instances connect over LAN/internet, share a synced
session clock, exchange allowlisted control input over UDP with rollback +
interpolation reconciliation, enforce channel ownership, pre-transfer tracks
into a verified local cache before a deck may route live sound, and expose it
all through a native Qt connect dialog + preferences page.

Ground rules for every task (also in `skills/ralph.md`):

- New code lives under `src/netmix/` unless the task says otherwise.
- Never add blocking calls, locks, or heap allocation to the audio callback.
- Tests are GoogleTest files `src/test/netmix*_test.cpp`; test fixture names
  start with `Netmix` so `ctest -R Netmix` catches them all.
- Every new `.cpp/.h/.ui` file must be registered in the root `CMakeLists.txt`.

---

## Phase 1.1 — Scaffold, Protocol, Session Clock

- [x] `1.1.1` Create `src/netmix/` module skeleton and CMake wiring
  - **Goal:** New directory `src/netmix/` with `netmixsessionmanager.h/.cpp` stub class (QObject, no logic), registered in root `CMakeLists.txt` alongside the other library sources. Builds clean.
  - **Touches:** `src/netmix/netmixsessionmanager.h`, `src/netmix/netmixsessionmanager.cpp`, `CMakeLists.txt`
  - **Success:** `cmake --build build --target mixxx-test` succeeds with the new files compiled.
  - **Tests:** None yet (compile is the gate).
  - **Difficulty:** Low
  - **Model:** Standard

- [x] `1.1.2` Protocol message definitions and serialization
  - **Goal:** `src/netmix/protocol.h/.cpp`: versioned wire protocol. Message enum (Hello, HelloAck, Ping, Pong, InputFrame, OwnershipClaim, OwnershipGrant, OwnershipDeny, OwnershipRelease, TrackOffer, TrackAccept, TrackChunk, TrackComplete, TrackReady, Bye), fixed little-endian header {magic, version, type, length}, QDataStream-based encode/decode helpers returning std::optional on malformed input. No sockets yet.
  - **Touches:** `src/netmix/protocol.h`, `src/netmix/protocol.cpp`, `CMakeLists.txt`, `src/test/netmixprotocol_test.cpp`
  - **Success:** Round-trip encode/decode for every message type; decode rejects bad magic, bad version, truncated payloads without crashing.
  - **Tests:** `NetmixProtocolTest` — round-trip per message type, fuzz-ish malformed-input cases.
  - **Difficulty:** Medium
  - **Model:** Standard

- [x] `1.1.3` SessionClock — fixed-rate tick from engine frame time
  - **Goal:** `src/netmix/sessionclock.h/.cpp`: converts accumulated audio frames (fed via `onFramesProcessed(int frames, int sampleRate)`) into a monotonically increasing 240 Hz tick counter. Pure logic, no Qt timers, no wall clock. Supports an offset (set by later clock-sync task) so both peers agree on absolute tick numbers.
  - **Touches:** `src/netmix/sessionclock.h`, `src/netmix/sessionclock.cpp`, `CMakeLists.txt`, `src/test/netmixsessionclock_test.cpp`
  - **Success:** Deterministic: identical frame-feed sequences yield identical tick sequences. Handles odd buffer sizes and sample-rate values without drift (rational accumulation, no float error growth).
  - **Tests:** `NetmixSessionClockTest` — tick progression at 44100/48000 Hz with varied buffer sizes, offset application, long-run drift bound.
  - **Difficulty:** Medium
  - **Model:** Standard

- [x] `1.1.4` NetmixSessionManager service owned by CoreServices
  - **Goal:** Flesh `NetmixSessionManager` into the long-lived service: constructed in `CoreServices` (near `BroadcastManager`, `src/coreservices.cpp`), owns SessionClock, exposes session state enum (Idle/Connecting/Connected/Degraded) as signals + a `[Netmix],status` ControlObject. No networking yet.
  - **Touches:** `src/netmix/netmixsessionmanager.h/.cpp`, `src/coreservices.h`, `src/coreservices.cpp`, `CMakeLists.txt`
  - **Success:** Mixxx starts and shuts down cleanly with the service instantiated; status CO readable.
  - **Tests:** Extend `src/test/netmixsessionclock_test.cpp` only if needed; primary gate is clean startup path compile + existing test suite still green.
  - **Difficulty:** Medium
  - **Model:** Standard

- [x] `1.1.5` Phase merge: release/1.1 → feat/rollback-network-mixing
  - **Goal:** Phase review passes, branch merges cleanly into the trunk.
  - **Touches:** todo-v1.md checkboxes
  - **Success:** All 1.1.x tasks checked; review returns PHASE_APPROVED.
  - **Tests:** Full `ctest -R Netmix` suite.
  - **Difficulty:** Low
  - **Model:** Standard

---

## Phase 1.2 — Control Capture & Apply

- [x] `1.2.1` Syncable-control allowlist
  - **Goal:** `src/netmix/controlallowlist.h/.cpp`: static table of synced ConfigKeys — per deck (`[ChannelN]`): play, cue_default, start, end, playposition seek (`playposition` set), rate, volume, pregain, filter/EQ knobs (`[EqualizerRackN_...]` and `[QuickEffectRackN_...]` superknob), hotcue_X_activate; global: `[Master],crossfader`. Each entry: ConfigKey pattern, control kind (continuous | discrete | seek), stable u16 wire id. Lookup both directions (key→id, id→key).
  - **Touches:** `src/netmix/controlallowlist.h`, `src/netmix/controlallowlist.cpp`, `CMakeLists.txt`, `src/test/netmixallowlist_test.cpp`
  - **Success:** Deterministic wire ids (stable across runs/versions); unknown keys map to nothing; every listed control resolves against a live ControlObject in a test harness.
  - **Tests:** `NetmixAllowlistTest` — bidirectional mapping, stability snapshot, kind classification.
  - **Difficulty:** Medium
  - **Model:** Standard

- [x] `1.2.2` ControlCapture — observe local control changes
  - **Goal:** `src/netmix/controlcapture.h/.cpp`: on session start, resolve allowlist entries to `ControlProxy` instances and connect `valueChanged`; emit `captured(tick, wireId, value)` stamped with current SessionClock tick. Must ignore changes whose `pSetter` is the netmix applier (echo suppression) — use the applier QObject sentinel per `ControlProxy::connectValueChanged` semantics (`src/control/controlproxy.h:162`).
  - **Touches:** `src/netmix/controlcapture.h`, `src/netmix/controlcapture.cpp`, `CMakeLists.txt`, `src/test/netmixcapture_test.cpp`
  - **Success:** Setting an allowlisted CO produces exactly one captured event; applier-originated sets produce zero; non-allowlisted COs produce zero.
  - **Tests:** `NetmixCaptureTest` — uses real ControlObjects in MixxxTest fixture; echo-suppression case.
  - **Difficulty:** Medium
  - **Model:** Standard

- [x] `1.2.3` ControlApplier — apply remote control events
  - **Goal:** `src/netmix/controlapplier.h/.cpp`: given `(wireId, value)`, resolve to CO and set it with the applier object as setter (so capture ignores it). Seek-kind events route through the normal `playposition`/seek COs (which feed EngineBuffer's lock-free `QueuedSeek`). Discrete kinds set exact value; continuous kinds support `applyRamped(wireId, target, ticks)` — linear ramp stepped by tick callback — for later interpolation reconciliation.
  - **Touches:** `src/netmix/controlapplier.h`, `src/netmix/controlapplier.cpp`, `CMakeLists.txt`, `src/test/netmixapplier_test.cpp`
  - **Success:** Applied values land on COs; capture (from 1.2.2) does not re-emit them; ramp converges to target in the requested tick count.
  - **Tests:** `NetmixApplierTest` — apply/echo-suppression integration with capture, ramp convergence.
  - **Difficulty:** Medium
  - **Model:** Standard

- [x] `1.2.4` InputFrame packing — per-tick aggregation with redundancy
  - **Goal:** `src/netmix/inputframe.h/.cpp`: aggregate captured events into per-tick InputFrames `{tick, [(wireId, value)...]}`; serialize batches of the last N frames (default 4) per UDP datagram (GGPO-style redundancy so one received packet fills small gaps). Dedup: same wireId twice in one tick keeps last value.
  - **Touches:** `src/netmix/inputframe.h`, `src/netmix/inputframe.cpp`, `src/netmix/protocol.h/.cpp` (InputFrame payload), `CMakeLists.txt`, `src/test/netmixinputframe_test.cpp`
  - **Success:** Encode/decode round-trip; batch of N frames stays under 1200 bytes for realistic event rates (assert in test); dedup correct.
  - **Tests:** `NetmixInputFrameTest` — round-trip, redundancy reconstruction with dropped datagrams, size bound.
  - **Difficulty:** Medium
  - **Model:** Standard

- [x] `1.2.5` Phase merge: release/1.2 → feat/rollback-network-mixing
  - **Goal:** Phase review passes, branch merges cleanly into the trunk.
  - **Touches:** todo-v1.md checkboxes
  - **Success:** All 1.2.x tasks checked; review returns PHASE_APPROVED.
  - **Tests:** Full `ctest -R Netmix` suite.
  - **Difficulty:** Low
  - **Model:** Standard

---

## Phase 1.3 — Transport: TCP Session + UDP Input + Clock Sync

- [x] `1.3.1` TCP session channel — listen, connect, handshake, heartbeat
  - **Goal:** `src/netmix/tcpsession.h/.cpp`: QTcpServer listen on configurable port (default 21200); QTcpSocket connect to peer IP/port. Hello/HelloAck handshake carries protocol version + peer display name + session parameters (tick rate, rollback window); version mismatch → clean reject. Length-prefixed framing over the socket using protocol.h codecs. Heartbeat + dead-peer detection (no traffic 5 s → Degraded, 15 s → Disconnected). All in Qt event loop thread — never the audio thread.
  - **Touches:** `src/netmix/tcpsession.h`, `src/netmix/tcpsession.cpp`, `CMakeLists.txt`, `src/test/netmixtcpsession_test.cpp`
  - **Success:** Two in-process instances (loopback) complete handshake and exchange messages; version mismatch rejected; teardown clean.
  - **Tests:** `NetmixTcpSessionTest` — loopback pair handshake, framing across split packets, mismatch reject, heartbeat timeout (shortened for test).
  - **Difficulty:** High
  - **Model:** Standard

- [x] `1.3.2` UDP input channel — datagram send/recv with sequencing
  - **Goal:** `src/netmix/udpchannel.h/.cpp`: QUdpSocket bound alongside the TCP port; sends InputFrame batches (from 1.2.4) with monotonically increasing sequence numbers; receiver drops duplicates/stale-older-than-window, tolerates reordering. Stats counters (sent, received, dropped, out-of-order) exposed for UI/diagnostics.
  - **Touches:** `src/netmix/udpchannel.h`, `src/netmix/udpchannel.cpp`, `CMakeLists.txt`, `src/test/netmixudpchannel_test.cpp`
  - **Success:** Loopback pair exchanges frames; artificially reordered/duplicated datagrams handled per spec; stats accurate.
  - **Tests:** `NetmixUdpChannelTest` — loopback exchange, reorder/dup/drop simulation.
  - **Difficulty:** Medium
  - **Model:** Standard

- [x] `1.3.3` Clock synchronization — NTP-lite offset estimation
  - **Goal:** `src/netmix/clocksync.h/.cpp`: periodic Ping/Pong over UDP measuring RTT and peer tick offset; sliding median filter (window 16) feeds `SessionClock::setOffset`. Session start: initiator proposes tick 0 epoch in HelloAck; both clocks converge within ±1 tick under symmetric latency. Exposes smoothed RTT for UI.
  - **Touches:** `src/netmix/clocksync.h`, `src/netmix/clocksync.cpp`, `src/netmix/protocol.h/.cpp` (Ping/Pong payloads), `CMakeLists.txt`, `src/test/netmixclocksync_test.cpp`
  - **Success:** Simulated latency/jitter scenarios converge to correct offset; asymmetric spike outliers filtered by median.
  - **Tests:** `NetmixClockSyncTest` — deterministic simulated channel, convergence + outlier cases.
  - **Difficulty:** High
  - **Model:** Standard

- [x] `1.3.4` Session state machine wired into NetmixSessionManager
  - **Goal:** Compose 1.3.1–1.3.3 into `NetmixSessionManager`: `hostSession(port)` / `joinSession(ip, port)` / `leaveSession()`; state transitions Idle→Connecting→Connected→Degraded→Idle drive signals and the status CO; capture→pack→UDP send and UDP recv→(buffer for phase 1.4) plumbing connected end to end behind a `#ifdef`-free runtime flag.
  - **Touches:** `src/netmix/netmixsessionmanager.h/.cpp`, `src/test/netmixsession_test.cpp`, `CMakeLists.txt`
  - **Success:** Loopback end-to-end: control change on instance A arrives as decoded InputFrame on instance B.
  - **Tests:** `NetmixSessionTest` — loopback end-to-end control event delivery, state transitions.
  - **Difficulty:** High
  - **Model:** Standard

- [x] `1.3.5` Phase merge: release/1.3 → feat/rollback-network-mixing
  - **Goal:** Phase review passes, branch merges cleanly into the trunk.
  - **Touches:** todo-v1.md checkboxes
  - **Success:** All 1.3.x tasks checked; review returns PHASE_APPROVED.
  - **Tests:** Full `ctest -R Netmix` suite.
  - **Difficulty:** Low
  - **Model:** Standard

---

## Phase 1.4 — Rollback Core

- [x] `1.4.1` InputBuffer — per-peer tick-indexed input ring
  - **Goal:** `src/netmix/inputbuffer.h/.cpp`: fixed-size ring (default 256 ticks) of remote InputFrames keyed by tick; frames marked confirmed (received) or predicted; `firstDivergentTick()` compares newly confirmed frames against what was predicted for those ticks. Local input ring kept too (needed for re-simulation).
  - **Touches:** `src/netmix/inputbuffer.h`, `src/netmix/inputbuffer.cpp`, `CMakeLists.txt`, `src/test/netmixinputbuffer_test.cpp`
  - **Success:** Late frames slot into correct ticks; divergence detection exact; ring wrap correct at capacity.
  - **Tests:** `NetmixInputBufferTest` — insertion order permutations, divergence, wrap.
  - **Difficulty:** Medium
  - **Model:** Standard

- [x] `1.4.2` Prediction — hold-last-input
  - **Goal:** `src/netmix/prediction.h/.cpp`: for ticks with no confirmed remote frame, predict empty event set with continuous controls holding last known value (i.e. prediction = "no new input"). Pluggable interface (strategy class) so velocity-extrapolation can be added later without touching callers.
  - **Touches:** `src/netmix/prediction.h`, `src/netmix/prediction.cpp`, `CMakeLists.txt`, `src/test/netmixprediction_test.cpp`
  - **Success:** Predicted frames deterministic given buffer state; interface allows swapping strategy.
  - **Tests:** `NetmixPredictionTest` — hold-last semantics across gaps.
  - **Difficulty:** Low
  - **Model:** Standard

- [x] `1.4.3` RollbackEngine — snapshot, rollback, re-simulate
  - **Goal:** `src/netmix/rollbackengine.h/.cpp`: per-tick snapshot of synced control state (map wireId→double, plus last-applied discrete event ids) into a ring covering the rollback window (default 8 ticks, max 30, from session params). On confirmed input diverging from prediction at tick T ≥ now−window: restore snapshot(T−1), re-apply confirmed remote + recorded local input from T..now via ControlApplier, re-predict the still-unconfirmed tail. Input older than the window: log, count, apply-forward only (no rollback) — bounded correction like GGPO. Runs in Qt thread on tick boundary; never touches audio thread directly.
  - **Touches:** `src/netmix/rollbackengine.h`, `src/netmix/rollbackengine.cpp`, `CMakeLists.txt`, `src/test/netmixrollback_test.cpp`
  - **Success:** Scripted scenarios (late fader move, late play press, conflicting prediction) end with identical final control state as a zero-latency reference run.
  - **Tests:** `NetmixRollbackTest` — reference-run equivalence across latency/drop scripts, window-exceeded path.
  - **Difficulty:** High
  - **Model:** Standard

- [x] `1.4.4` Interpolation reconciliation for continuous controls
  - **Goal:** After a rollback correction, continuous controls must not snap: route corrections through `ControlApplier::applyRamped` over `min(correction_magnitude-scaled, 4) ticks`; discrete controls re-fire exact. Ramp cancellation: a newer correction or fresh confirmed input on the same control supersedes an in-flight ramp.
  - **Touches:** `src/netmix/rollbackengine.h/.cpp`, `src/netmix/controlapplier.h/.cpp`, `src/test/netmixrollback_test.cpp`
  - **Success:** Corrected fader path is continuous (no inter-tick jump larger than ramp step) while converging to reference final value.
  - **Tests:** Extend `NetmixRollbackTest` — continuity assertion on corrected trajectories, ramp supersession.
  - **Difficulty:** Medium
  - **Model:** Standard

- [ ] `1.4.5` Optional 64th-note quantization of input events
  - **Goal:** `src/netmix/quantizer.h/.cpp`: when enabled (session param + `[Netmix],quantize` CO), snap event ticks to the nearest 64th-note boundary derived from the sync-leader BPM (`EngineSync`, `src/engine/sync/enginesync.h`) and session tick rate; applies symmetrically on both peers before capture-send and before apply, so replay stays consistent. Discrete transport events snap; continuous knob streams pass through unquantized (only their timestamps snap).
  - **Touches:** `src/netmix/quantizer.h`, `src/netmix/quantizer.cpp`, `src/netmix/netmixsessionmanager.cpp`, `CMakeLists.txt`, `src/test/netmixquantizer_test.cpp`
  - **Success:** At 120 BPM / 240 Hz ticks, 64th grid = 7.8125 ticks — snapping matches hand-computed boundaries; disabled path is byte-identical passthrough.
  - **Tests:** `NetmixQuantizerTest` — grid math across BPMs, enable/disable, both-sides symmetry.
  - **Difficulty:** Medium
  - **Model:** Standard

- [ ] `1.4.6` Phase merge: release/1.4 → feat/rollback-network-mixing
  - **Goal:** Phase review passes, branch merges cleanly into the trunk.
  - **Touches:** todo-v1.md checkboxes
  - **Success:** All 1.4.x tasks checked; review returns PHASE_APPROVED.
  - **Tests:** Full `ctest -R Netmix` suite.
  - **Difficulty:** Low
  - **Model:** Standard

---

## Phase 1.5 — Channel Ownership & Locks

- [ ] `1.5.1` Ownership model and protocol
  - **Goal:** `src/netmix/channelownership.h/.cpp`: per-channel owner state (Unowned | OwnedLocal | OwnedRemote | PendingClaim); pre-assignment map exchanged in handshake (session params list channels each peer owns ahead of time); Claim/Grant/Deny/Release messages over TCP for open channels.
  - **Touches:** `src/netmix/channelownership.h`, `src/netmix/channelownership.cpp`, `src/netmix/protocol.h/.cpp`, `CMakeLists.txt`, `src/test/netmixownership_test.cpp`
  - **Success:** Pre-assigned channels enter session already owned; claim on unowned channel grants; claim on owned channel denies.
  - **Tests:** `NetmixOwnershipTest` — state machine transitions, handshake pre-assignment.
  - **Difficulty:** Medium
  - **Model:** Standard

- [ ] `1.5.2` Mutex-style reservation with race resolution
  - **Goal:** Simultaneous claims (both peers claim same open channel before seeing each other's claim) resolve deterministically: lower peer id (assigned at handshake, initiator=0) wins; loser auto-converts to Deny locally. Reservation auto-releases on disconnect and on explicit release; optional idle timeout (default off).
  - **Touches:** `src/netmix/channelownership.h/.cpp`, `src/test/netmixownership_test.cpp`
  - **Success:** Scripted simultaneous-claim scenario converges to single owner on both simulated peers — no clobber, no deadlock.
  - **Tests:** Extend `NetmixOwnershipTest` — race scripts, disconnect release.
  - **Difficulty:** Medium
  - **Model:** Standard

- [ ] `1.5.3` Enforcement in capture and apply paths
  - **Goal:** ControlCapture drops (does not send) events for channels the local peer doesn't own; ControlApplier rejects incoming events for channels the remote peer doesn't own (defense in depth). Global controls (crossfader) treated as a channel-like resource with its own owner slot. Local UI attempts on peer-owned channels are reverted via applier (value snaps back) and counted for a UI hint.
  - **Touches:** `src/netmix/controlcapture.cpp`, `src/netmix/controlapplier.cpp`, `src/netmix/channelownership.h/.cpp`, `src/test/netmixownership_test.cpp`
  - **Success:** End-to-end loopback: peer-owned channel ignores local wiggle, applies remote input; owned channel does the reverse.
  - **Tests:** Extend `NetmixOwnershipTest` + `NetmixSessionTest` — enforcement both directions.
  - **Difficulty:** Medium
  - **Model:** Standard

- [ ] `1.5.4` Phase merge: release/1.5 → feat/rollback-network-mixing
  - **Goal:** Phase review passes, branch merges cleanly into the trunk.
  - **Touches:** todo-v1.md checkboxes
  - **Success:** All 1.5.x tasks checked; review returns PHASE_APPROVED.
  - **Tests:** Full `ctest -R Netmix` suite.
  - **Difficulty:** Low
  - **Model:** Standard

---

## Phase 1.6 — Track Pre-Transfer & Remote Cache

- [ ] `1.6.1` Remote track cache directory and index
  - **Goal:** `src/netmix/trackcache.h/.cpp`: cache dir `<settingsdir>/netmix_cache/` (settings dir via `ConfigObject::getSettingsPath`, `src/preferences/configobject.cpp:209`); files stored as `<sha256>.<ext>`; JSON index file mapping hash → {original filename, size, source peer, added timestamp, verified flag}. Lookup, insert, verify (re-hash), evict APIs. Corrupt/missing index rebuilds from directory scan.
  - **Touches:** `src/netmix/trackcache.h`, `src/netmix/trackcache.cpp`, `CMakeLists.txt`, `src/test/netmixtrackcache_test.cpp`
  - **Success:** Insert/lookup/verify round-trip with temp dirs; index rebuild works; no writes outside cache dir (path traversal guarded).
  - **Tests:** `NetmixTrackCacheTest` — CRUD, rebuild, traversal-attempt rejection.
  - **Difficulty:** Medium
  - **Model:** Standard

- [ ] `1.6.2` Chunked file transfer over TCP with verify + resume
  - **Goal:** `src/netmix/tracktransfer.h/.cpp`: sender streams TrackOffer {hash, size, name, mime} → receiver TrackAccept {have-bytes for resume} → TrackChunk (64 KiB) sequence → TrackComplete; receiver writes to `<hash>.partial`, renames after full sha256 verify. Transfers run on the TCP session socket interleaved with control messages (chunk messages yield to pending control traffic — bounded queue). Progress signals for UI.
  - **Touches:** `src/netmix/tracktransfer.h`, `src/netmix/tracktransfer.cpp`, `src/netmix/protocol.h/.cpp`, `CMakeLists.txt`, `src/test/netmixtracktransfer_test.cpp`
  - **Success:** Loopback transfer of a multi-MB temp file verifies byte-identical; kill-and-reconnect resumes from partial; corrupted chunk fails verify and re-requests.
  - **Tests:** `NetmixTrackTransferTest` — full transfer, resume, corruption, interleaving with control messages.
  - **Difficulty:** High
  - **Model:** Standard

- [ ] `1.6.3` Queue-triggered background send + readiness handshake
  - **Goal:** Wire into deck loading: when a track is loaded/queued on a session deck the local peer owns (hook `PlayerManager`/`BaseTrackPlayer` `loadingTrack` signal, `src/mixer/basetrackplayer.h:70`), NetmixSessionManager hashes the file, offers it, and starts background transfer if the remote cache lacks it. When remote confirms verified cache entry it sends TrackReady; both-ready state tracked per deck.
  - **Touches:** `src/netmix/netmixsessionmanager.h/.cpp`, `src/netmix/trackcache.cpp`, `src/test/netmixsession_test.cpp`
  - **Success:** Loopback: loading a track on owned deck ends with remote cache containing verified copy + both-ready flag set.
  - **Tests:** Extend `NetmixSessionTest` — load→transfer→ready flow.
  - **Difficulty:** High
  - **Model:** Standard

- [ ] `1.6.4` Live-sound gating + remote deck load + analysis
  - **Goal:** A session deck may route live sound only when its track is both-ready: gate by holding the channel's netmix-ready CO and muting channel output via existing gain path until ready (no engine-callback changes — use channel volume/enable COs). Remote side auto-loads the cached file to the mirrored deck via `Track::newTemporary(path)` (`src/track/track.h:42`) through `PlayerManager::slotLoadTrackToPlayer`, then schedules analysis (`Library::analyzeTracks`) so beats/waveform populate.
  - **Touches:** `src/netmix/netmixsessionmanager.cpp`, `src/netmix/controlapplier.cpp`, `src/test/netmixsession_test.cpp`
  - **Success:** Deck stays silent until both-ready; remote deck ends up with the cached track loaded and analysis queued.
  - **Tests:** Extend `NetmixSessionTest` — gating + remote load; manual smoke documented in `docs/netmix-manual-test.md`.
  - **Difficulty:** High

- [ ] `1.6.5` Cue point / hotcue / loop metadata transfer
  - **Goal:** Local analysis alone does not reproduce the owning DJ's hotcues, main cue, intro/outro, or saved loops — `hotcue_X_activate` messages replicated over the wire (allowlist in `1.4.1`) are meaningless on the remote deck unless its `Track` has matching `CuePointer` entries at the same hotcue indices. Serialize the owning peer's `Track::getCuePoints()` (`src/track/track.h:342`, `Cue` fields: type, hotcue index, start/end sample position, color, label — `src/track/cue.h:15`) into a `CueSnapshot` sent alongside (or immediately after) the `TrackComplete` message from `1.6.2`. On receipt, apply via `Track::setCuePoints()` (`src/track/track.h:348`) before the deck is marked ready in `1.6.3`'s both-ready handshake — analysis-derived beatgrid/waveform still comes from local `Library::analyzeTracks`, but cue points are the sender's authoritative values, not re-derived.
  - **Touches:** `src/netmix/protocol.h/.cpp`, `src/netmix/tracktransfer.cpp`, `src/netmix/netmixsessionmanager.cpp`, `src/test/netmixtracktransfer_test.cpp`
  - **Success:** Loopback transfer carries hotcues/main cue/loops byte-for-byte (position + label + color); remote `Track::getCuePoints()` matches sender's before both-ready fires; a deck is never marked ready with stale/partial cue data.
  - **Tests:** Extend `NetmixTrackTransferTest` — cue snapshot round-trip, ready-gate ordering.
  - **Difficulty:** Medium
  - **Model:** Standard

- [ ] `1.6.6` Phase merge: release/1.6 → feat/rollback-network-mixing
  - **Goal:** Phase review passes, branch merges cleanly into the trunk.
  - **Touches:** todo-v1.md checkboxes
  - **Success:** All 1.6.x tasks checked; review returns PHASE_APPROVED.
  - **Tests:** Full `ctest -R Netmix` suite.
  - **Difficulty:** Low
  - **Model:** Standard

---

## Phase 1.7 — UI: Connect Dialog, Preferences, Indicators

- [ ] `1.7.1` DlgNetmixConnect dialog + main menu entry
  - **Goal:** `src/dialog/dlgnetmixconnect.h/.cpp/.ui` (pattern: `DlgAbout`, `src/dialog/dlgabout.cpp:13`): host-or-join choice, peer IP/port fields, display name, per-deck ownership pre-assignment combo (Local/Remote/Open), connect/disconnect button, live status + RTT label bound to NetmixSessionManager signals. Menu: `WMainMenuBar` signal `showNetmixConnect` (pattern `src/widget/wmainmenubar.cpp` showAbout) wired in `MixxxMainWindow` (`src/mixxxmainwindow.cpp:889` area).
  - **Touches:** `src/dialog/dlgnetmixconnect.h/.cpp/.ui`, `src/widget/wmainmenubar.h/.cpp`, `src/mixxxmainwindow.h/.cpp`, `CMakeLists.txt`
  - **Success:** Dialog opens from menu, drives host/join/leave on the manager, reflects state changes live.
  - **Tests:** Compile + existing suite green; manual smoke steps appended to `docs/netmix-manual-test.md`.
  - **Difficulty:** Medium
  - **Model:** Standard

- [ ] `1.7.2` Preferences page DlgPrefNetmix
  - **Goal:** `src/preferences/dialog/dlgprefnetmix.h/.cpp/.ui` registered in `DlgPreferences` constructor (`src/preferences/dialog/dlgpreferences.cpp:107` area): listen port, default display name, rollback window (ticks), quantize-to-64ths default toggle, cache directory display + size + clear button. Persists via UserSettings.
  - **Touches:** `src/preferences/dialog/dlgprefnetmix.h/.cpp/.ui`, `src/preferences/dialog/dlgpreferences.cpp`, `CMakeLists.txt`
  - **Success:** Settings round-trip through apply/cancel/reset-to-defaults; values consumed by NetmixSessionManager at next session start.
  - **Tests:** Compile + suite green; slotApply/slotUpdate manual check documented.
  - **Difficulty:** Medium
  - **Model:** Standard

- [ ] `1.7.3` Session status ControlObjects for skins/controllers
  - **Goal:** Expose per-session COs under `[Netmix]`: status, rtt_ms, rollback_count, peer_connected; per-channel: `netmix_owner` (0 local/1 remote/2 open), `netmix_ready`. Documented in a short section appended to `docs/plan.md`. Skins/controllers can bind without new widget work.
  - **Touches:** `src/netmix/netmixsessionmanager.h/.cpp`, `src/netmix/channelownership.cpp`, `docs/plan.md`, `src/test/netmixsession_test.cpp`
  - **Success:** COs exist, update on state changes, readable via ControlProxy in tests.
  - **Tests:** Extend `NetmixSessionTest` — CO presence + update assertions.
  - **Difficulty:** Low
  - **Model:** Standard

- [ ] `1.7.4` Phase merge: release/1.7 → feat/rollback-network-mixing
  - **Goal:** Phase review passes, branch merges cleanly into the trunk. v1 complete.
  - **Touches:** todo-v1.md checkboxes
  - **Success:** All 1.7.x tasks checked; review returns PHASE_APPROVED; full manual smoke per `docs/netmix-manual-test.md` documented.
  - **Tests:** Full `ctest -R Netmix` suite + whole mixxx-test suite.
  - **Difficulty:** Low
  - **Model:** Standard
