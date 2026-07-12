# Netmix — Rollback Network Mixing for Mixxx

Source spec: `.ctutor_docs/ctutor-rollback.md`

## Concept

Two DJs run Mixxx on separate machines and mix a shared set of decks together.
No audio is streamed. Only *control input* (knob, fader, transport events) plus a
synced session timer travel over the wire. Each instance plays audio locally
from a locally cached copy of every track, so there is zero audio latency.
Late/lost remote input is handled with fighting-game style **rollback
netcode**: local playback never waits; remote input is predicted, and when the
real input arrives late the control state is rolled back a bounded number of
ticks and re-simulated, with smooth interpolation as the default reconciliation
for continuous controls.

## Scope decisions (2026-07-12, confirmed with owner)

| Decision | Choice |
|---|---|
| Connect UI | Native Qt dialog (`DlgAbout`-style), wired into `WMainMenuBar` |
| Transport | UDP for per-tick input frames, TCP for handshake / control / file transfer |
| v1 scope | Core sync + channel ownership/locks + track pre-transfer/cache + optional 64th-note quantize |
| Deferred to v2 | Rotating pub/private key track-cache encryption; voice chat channel |
| Ralph trunk | `feat/rollback-network-mixing` (upstream `master` untouched) |

## Architecture

New module: `src/netmix/`. One long-lived service, `NetmixSessionManager`,
owned by `CoreServices` (created alongside `BroadcastManager`,
`src/coreservices.cpp`).

```
┌────────────── local Mixxx ──────────────┐        ┌───── remote Mixxx ─────┐
│ ControlCapture ──► InputFrame packer ───┼─ UDP ──┼─► InputBuffer ─► Rollback
│   (valueChanged hooks, allowlisted)     │        │      │ predict/confirm │
│ RollbackEngine ◄── InputBuffer ◄────────┼── UDP ─┼── InputFrame packer    │
│      │ re-sim + interpolate             │        │                        │
│ ControlApplier ──► ControlObjects       │  TCP   │  handshake, clock sync │
│ SessionClock (240 Hz tick, synced)      │◄──────►│  ownership, file xfer  │
│ TrackTransfer ──► netmix_cache/         │        │                        │
└─────────────────────────────────────────┘        └────────────────────────┘
```

### Key design points, grounded in the codebase

- **Capture**: every syncable control is observed centrally.
  `ControlDoublePrivate` emits `valueChanged(double, QObject* pSetter)`
  (`src/control/control.h:164`); `ControlProxy::connectValueChanged`
  filters self-originated sets, so remote applies (done with the applier as
  `pSetter`) never echo back. Only an explicit **allowlist** of ConfigKeys is
  synced: per-deck volume/EQ/filter knobs, crossfader, play/cue/seek/rate,
  effect knobs. Library browsing, prefs, skin controls never sync.
- **Apply**: `ControlObject::set()` is thread-safe via `ControlValueAtomic`
  (`src/control/controlvalue.h:73`); the engine callback reads current values
  each buffer, so applying control events from the Qt thread at tick
  granularity is safe and sample-accurate enough. Seeks go through the
  existing lock-free `QueuedSeek` path (`src/engine/enginebuffer.cpp:410`).
- **Session tick**: fixed-rate logical tick (default **240 Hz**) derived from
  audio callback frame accumulation (`EngineMixer::process`,
  `src/engine/enginemixer.cpp:361`) — not wall clock. Peers agree on tick 0 at
  session start; ongoing offset estimation via UDP ping (NTP-lite) keeps the
  timers converged.
- **Rollback**: control state is tiny (a map of allowlisted ConfigKey →
  double), so a ring of per-tick state snapshots over the rollback window
  (default 8 ticks ≈ 33 ms, max 30) is cheap. On late-arriving remote input
  that contradicts prediction: restore snapshot at the divergent tick,
  re-apply confirmed + local input, re-predict forward. Continuous controls
  (faders/knobs) reconcile by **smooth interpolation** toward the corrected
  value over a short ramp instead of snapping; discrete controls
  (play/cue/hotcue) re-fire exactly.
- **Prediction**: hold-last-input. Knobs mid-gesture predict continued value;
  a wrong prediction is at most `rollback_window` ticks of error, hidden by
  the interpolation ramp.
- **Quantize (optional)**: event ticks snap to a 64th-note grid computed from
  the deck's `Beats` (`Beats::findNBeatsFromPosition`, `src/track/beats.h`)
  and the sync leader's BPM (`src/engine/sync/enginesync.h`). Off by default.
- **Channel ownership**: channels either pre-assigned an owner at session
  setup or "open". Open channels take a mutex-like reservation: request →
  grant/deny over TCP, deterministic tie-break by peer id, auto-release on
  timeout/disconnect. Capture refuses to send, and applier refuses to apply,
  input for channels the sender doesn't hold.
- **Track pre-transfer**: assigning/queueing a track to a session deck
  triggers a background chunked TCP transfer into
  `<settingsdir>/netmix_cache/` (sha256-named, resumable, verified).
  A deck routes live sound only after **both** peers confirm the cached copy
  (readiness handshake). Load uses `Track::newTemporary(path)`
  (`src/track/track.h:42`) — no library DB dependency — then schedules
  analysis so waveforms/beats appear. Cue points, hotcues, and saved loops
  are **not** re-derivable from analysis alone — the sender's
  `Track::getCuePoints()` is serialized and applied via `setCuePoints()` on
  the remote side before ready-gating, so `hotcue_X_activate` messages land
  on the same positions on both decks.
- **No audio on the wire** in v1. Voice chat (v2) will reuse the
  `EngineMicrophone` capture path (`src/engine/channels/enginemicrophone.h`)
  with an Opus encoder, modeled on `ShoutConnection`'s sidechain pattern.
- **Determinism caveat** (from engine audit): `EnginePregain`'s replay-gain
  fade uses a wall-clock timer (`src/engine/enginepregain.cpp:82-99`). We sync
  control events, not audio state, so outputs on the two machines may differ
  by inaudible fade phase — accepted, documented, not a blocker.

## Milestones

| Milestone | File | Contents |
|---|---|---|
| v1 core | `docs/todo-v1.md` | Phases 1.1–1.7: scaffold, capture/apply, transport, rollback, ownership, track transfer, UI |
| v2 extras | `docs/todo-v2.md` | Phase 2.1 rotating-key cache encryption, phase 2.2 voice chat |

## Quality gates

- Build: `cmake --build build --target mixxx-test`
- Tests: `ctest --test-dir build -R 'Netmix'` (new tests live in
  `src/test/netmix*_test.cpp`, GoogleTest, registered in root
  `CMakeLists.txt` next to the other `src/test/` entries)
- Format/lint: pre-commit hooks (`.pre-commit-config.yaml` — clang-format
  19.1.3 via `tools/clang_format.py`, codespell, gersemi)
- Hard rules: no blocking calls or heap allocation added to the audio
  callback; all cross-thread engine communication via existing lock-free
  primitives (`ControlValueAtomic`, FIFO); protocol structs versioned from
  day one.

## Ralph workflow

`scripts/ralph.sh` — same loop as figby/Zoid/Zoidmatter, adapted:
trunk is `feat/rollback-network-mixing`; phases live on `release/X.Y`
branches cut from the trunk and merge back into it; tasks on `task-X.Y.Z`
branches. All agents default to `opencode-go/deepseek-v4-flash`,
overridable via env vars. See `skills/ralph.md`.
