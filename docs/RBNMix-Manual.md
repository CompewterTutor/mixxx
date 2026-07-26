# RBNMix Manual — Rollback Network Mixing

RBNMix ("Netmix") is an experimental fork feature (branch
`feat/rollback-network-mixing`) that lets two DJs on separate machines mix a
shared set of decks together over a network connection with minimal
perceived latency. It is not a general-purpose remote-collab tool for
arbitrary numbers of peers — v1 targets exactly **two peers**, up to five
shared decks, and no live audio streaming.

This manual is split into:

1. [Feature overview](#feature-overview) — what RBNMix does and how it works, at a glance.
2. [Quickstart](#quickstart) — shortest path to a working session.
3. [Detailed setup guide](#detailed-setup-guide) — walkthrough with every step explained.
4. [Reference: all options](#reference-all-options) — every preference, dialog field, and ControlObject.
5. [Troubleshooting](#troubleshooting) and [Known limitations](#known-limitations).

---

## Feature overview

**The core idea:** no audio ever crosses the wire. Both machines hold a
local, verified copy of every track assigned to a shared deck and play it
locally, so there is zero audio-path latency. Only small control messages —
knob/fader positions, play/cue/seek events, and a synced session clock —
travel over the network. Because those messages are tiny, RBNMix can afford
to use **rollback netcode**, the technique fighting games use to hide
network latency between players:

- **No waiting.** Local playback and local input are never blocked on the
  network. The engine always renders immediately using its best guess of
  the remote peer's state.
- **Prediction.** While waiting for a remote input frame, RBNMix assumes the
  remote peer's controls are holding steady (hold-last-input prediction).
- **Rollback + re-simulation.** When a real remote input frame arrives late
  and contradicts the prediction, RBNMix rewinds control state to the last
  correct tick, re-applies the confirmed input, and re-predicts forward —
  all within a bounded rollback window (a handful of ticks, on the order of
  tens of milliseconds).
- **Smooth reconciliation.** Continuous controls (faders, knobs, EQs) never
  snap when corrected — they interpolate smoothly to the corrected value.
  Discrete controls (play, cue, hotcues) simply re-fire exactly, since a
  fighting-game-style hard cut is inaudible/expected for those.

**Other headline features:**

- **Channel ownership.** Each shared deck (channel) can be pre-assigned to
  one peer, or left "open" and claimed on demand via a mutex-like
  reservation, preventing both DJs from fighting over the same deck.
- **Track pre-caching.** Assigning or queueing a track to a shared deck
  triggers a background transfer to the other peer's local cache. A deck
  only starts routing live sound once **both** sides confirm the track is
  cached and ready — this is what makes zero-latency local playback
  possible in the first place.
- **Cue/hotcue sync.** Cue points, hotcues, and saved loops travel with the
  track transfer, so both sides see identical marks without relying on
  re-analysis to reproduce them.
- **Optional quantize.** Control events can optionally snap to a 64th-note
  grid derived from the deck's beatgrid, off by default.
- **Skin/controller visibility.** Session status, RTT, rollback count, peer
  connection state, and per-channel ownership/readiness are all exposed as
  read-only ControlObjects, so skins and MIDI/HID controllers can surface
  session state without extra widget work.

**What it deliberately does *not* do (v1):** stream audio, support more than
two peers, encrypt the track cache (planned for a later phase), or carry
voice chat (planned for a later phase).

---

## Quickstart

Fastest path to a working two-peer session. Both DJs should do steps 1–2 in
parallel; one of you hosts, the other joins.

1. **Agree on host/port.** One DJ hosts (needs to be reachable on a UDP+TCP
   port, default **21200** — port-forward or use a VPN/LAN if behind NAT).
   The other DJ joins using the host's public/LAN IP and that port.
2. **Open the connect dialog.** In Mixxx, go to **Options → Netmix
   Connect...**
3. **Host side:** select **Host**, leave/set the port (default `21200`),
   enter a display name, assign your decks (e.g. Channel 1 = Local, Channel
   2 = Remote, Channels 3–4 = Open), click **Connect**.
4. **Join side:** select **Join**, enter the host's IP and port, enter a
   display name, mirror the deck assignment (your "Local" decks should be
   the host's "Remote" decks and vice versa), click **Connect**.
5. **Wait for "Connected".** Status goes Idle → Connecting → Connected.
   RTT should populate with a real millisecond value.
6. **Load a track onto a shared deck.** The deck's `netmix_ready` indicator
   goes to 1 only once the track is cached and verified on both sides — the
   deck stays silent/inactive on the remote side until then.
7. **Mix.** Anything on the allowlist (volume, EQ, filter, crossfader,
   play/cue/seek, rate, effect knobs) on a deck you own is now synced to
   your peer with rollback-hidden latency.
8. **Disconnect** from the same dialog when done; status returns to Idle.

If step 5 never reaches "Connected", see [Troubleshooting](#troubleshooting).

---

## Detailed setup guide

### 1. Requirements

- Two machines each running this fork, built from `feat/rollback-network-mixing`.
- Network path between the two machines: a routable IP and one open UDP+TCP
  port pair (same port number for both protocols, default `21200`) on the
  hosting side. LAN, VPN (Tailscale/WireGuard, etc.), or port-forwarded WAN
  all work; RBNMix does not do NAT traversal itself.
- Enough local disk space for the session's tracks — the cache directory
  holds a full local copy of every track transferred (see [Cache
  directory](#cache-directory)).

### 2. Configure preferences (optional, once per machine)

Before connecting, you can optionally set defaults in
**Preferences → Netmix**:

- **Listen port** — the UDP/TCP port used when you host. Default `21200`.
- **Default display name** — pre-fills the connect dialog's name field.
- **Rollback window (ticks)** — how many ticks of history the engine keeps
  for rollback/re-simulation. Default `8` ticks (~33 ms at the 240 Hz
  session tick rate). See [Rollback window](#rollback-window) for tuning
  guidance.
- **Quantize to 64ths (default)** — whether new sessions start with 64th-note
  quantization on. Default off.
- **Cache directory** — display-only path plus current size, with a
  **Clear Cache** button to reclaim disk space between sessions.

These are just session defaults; the connect dialog can still override port
and display name per-session.

### 3. Open the connect dialog

**Options → Netmix Connect...** opens `DlgNetmixConnect`. Fresh state is:
Host selected, port `21200`, display name empty, all deck combos on "Open",
Connect enabled, Disconnect disabled, status "Idle", RTT "-- ms".

### 4. Choose Host or Join

- **Host**: you are the rendezvous point. The peer IP field is not needed
  for the local side; the port field is what the other DJ needs to reach
  you on.
- **Join**: enter the host's IP address and port exactly as the host is
  listening. An invalid address (e.g. `notanip`) is rejected in-dialog with
  a "Invalid peer IP address" message — no crash, no partial connection.

### 5. Set your display name

Freeform text shown to identify you to your peer during the session (and
usable by skins). Not authentication — it's a label, not a password.

### 6. Assign channel ownership

Each of the (up to five) deck channels gets a combo box: **Local**,
**Remote**, or **Open**.

- **Local** — you own this deck; your controls drive it and are sent to
  your peer.
- **Remote** — your peer owns this deck; you receive their control events
  and apply them locally, your own input to that deck is ignored/blocked.
- **Open** — nobody owns it yet. Either side can request it once connected;
  requests are granted/denied over the session's TCP link, ties are broken
  deterministically by peer id, and an idle/disconnected holder's
  reservation auto-releases.

The two sides' assignments should be mirror images of each other for a
given channel (your Local = their Remote) — mismatched assignments will
generally result in one side's input silently having no effect on that
deck, since applying happens only to channels the sender is confirmed to
own.

### 7. Connect

Click **Connect**. All input fields lock while a session is active. Status
progresses Idle → Connecting → Connected (or stays on an error state if the
handshake fails — see [Troubleshooting](#troubleshooting)). Once Connected,
RTT begins reporting real numbers from the ongoing clock-sync ping.

### 8. Load and cache tracks

Load or queue a track to any deck you own (or an open deck you've claimed).
This triggers a background chunked transfer of that track to your peer's
cache directory (see [Cache directory](#cache-directory)). The deck will
not route live sound on either side until **both** peers confirm the same
track is cached and verified — watch `netmix_ready` for that channel (see
[ControlObjects](#session-status-controlobjects)) or the deck's skin
indicator if your skin surfaces it. Cue points, hotcues, and loops travel
with the transfer and land at identical positions on both sides.

### 9. Mix

Play, cue, seek, adjust EQ/filter/volume/rate, hit hotcues, move the
crossfader — anything on the [sync allowlist](#synced-controls-allowlist)
on a deck you own is sent to your peer and applied there via rollback
netcode, so your peer hears your moves with the network's actual RTT hidden
behind prediction + interpolation rather than added as raw lag.

### 10. Optional: quantize

If enabled (session default from Preferences, no per-session toggle in the
connect dialog in v1), event ticks snap to a 64th-note grid computed from
the deck's beatgrid and the sync leader's BPM before being sent/applied.
Useful for keeping a tight, sample-locked mix between the two rooms; leave
off if you want free-form knob movement to feel exactly like local mixing.

### 11. Disconnect

Click **Disconnect** at any time. Status returns to Idle, inputs re-enable,
RTT resets to "-- ms". Reopening the dialog afterward starts from the same
default-ish state (previously entered values may or may not persist,
depending on session vs. saved-preference fields — see
[reference](#reference-all-options)).

---

## Reference: all options

### Connect dialog (`Options → Netmix Connect...`)

| Field | Type | Default | Notes |
|---|---|---|---|
| Host / Join | radio | Host | Selects rendezvous role for this session. |
| Peer IP | text | empty | Only meaningful in Join mode; validated on Connect. |
| Port | spinbox | `21200` | Used for both the TCP session and UDP input channel. |
| Display name | text | empty (or Preferences default) | Freeform label shown to your peer. |
| Channel 1–4/5 ownership | combo (Local/Remote/Open) | Open | Pre-assignment; Open channels are claimable post-connect. |
| Connect | button | — | Disabled while already connected. |
| Disconnect | button | — | Disabled while idle. |
| Status label | read-only | "Idle" | Idle / Connecting... / Connected / error text. |
| RTT label | read-only | "-- ms" | Live round-trip time once connected; resets on disconnect. |

### Preferences → Netmix

| Setting | Default | Effect |
|---|---|---|
| Listen port | `21200` | Port used when hosting; also the default pre-filled in the connect dialog. |
| Default display name | empty | Pre-fills the connect dialog's display name field. |
| Rollback window (ticks) | `8` | Max ticks of state history kept for rollback/re-simulation; see below. |
| Quantize to 64ths (default) | off | Whether new sessions start with quantization enabled. |
| Cache directory | `<settingsdir>/netmix_cache/` | Display-only; shows path and current size. |
| Clear Cache | button | Deletes cached track copies to reclaim disk space. Safe between sessions; do not clear mid-session. |

Preferences apply/cancel/reset-to-defaults round-trip normally and are
consumed by the session manager the *next* time a session starts — changing
them mid-session has no effect until you reconnect.

#### Rollback window

Controls how many ticks (at the 240 Hz internal session tick rate) of
control-state history are kept so a late remote input frame can trigger a
rewind-and-resimulate instead of being dropped or snapped in immediately.
Larger values tolerate more jitter/latency before a correction becomes
audible as a discrete jump, at the cost of a larger correction (still
smoothed by interpolation) when one does occur. Default `8` ticks is
roughly 33 ms; the practical ceiling is 30 ticks (~125 ms) beyond which
rollback stops being a good trade against just accepting perceived lag.

#### Synced controls allowlist

Only an explicit allowlist of controls is ever sent over the wire:
per-deck volume, EQ, filter knobs, crossfader, play/cue/seek/rate, and
effect knobs. Library browsing, preferences, and skin-only controls are
never synced regardless of channel ownership.

#### Cache directory

Location: `<settingsdir>/netmix_cache/`. Files are content-addressed
(sha256-named), transferred in resumable chunks, and verified before a
deck is allowed to go "ready". The cache is not currently encrypted or
automatically pruned — use **Clear Cache** in Preferences to reclaim space.

### Session status ControlObjects

Read-only; usable from skins and controller mappings without extra widget
work.

| ConfigKey | Range | Description |
|---|---|---|
| `[Netmix], status` | 0–3 | SessionState: 0=Idle, 1=Connecting, 2=Connected, 3=Degraded |
| `[Netmix], rtt_ms` | double | Round-trip time in ms from clock sync (0.0 when unavailable) |
| `[Netmix], rollback_count` | double | Total rollback events since session start |
| `[Netmix], peer_connected` | 0/1 | 1 when the TCP session is Connected |
| `[Channel0-4], netmix_owner` | 0/1/2 | 0=local owner, 1=remote owner, 2=open |
| `[Channel1-4], netmix_ready` | 0/1 | 1 once both peers have the deck's track cached and verified |

---

## Troubleshooting

- **Stuck on "Connecting..."** — check that the hosting side's port
  (default `21200`) is open for both TCP and UDP, not just one; a firewall
  or NAT blocking only UDP will hang the input channel even though the TCP
  handshake looks fine.
- **"Invalid peer IP address"** — the Join-mode IP field needs a literal
  IPv4/IPv6 address, not a hostname, in v1.
- **Deck stays silent after loading a track** — check `netmix_ready` for
  that channel; the deck will not route live sound until both sides finish
  caching and verifying the track. Large tracks over a slow link take
  longer to pre-transfer.
- **A deck won't respond to your input** — confirm `netmix_owner` for that
  channel; you can only send/have applied input for a channel you currently
  own (pre-assigned Local, or an Open channel you've successfully claimed).
- **High/rising `rollback_count`** — expected under real network jitter;
  it's informational, not necessarily a problem. If mixing starts to sound
  audibly glitchy, consider raising the rollback window in Preferences.

## Known limitations

- Two peers only in v1; no session recording of the "shared" mix separate
  from each side's local rendering.
- No live audio/voice channel yet.
- Track cache is unencrypted (rotating pub/private key encryption is a
  planned follow-up).
- No NAT traversal/relay — one side must be reachable on the configured
  port.
- `EnginePregain`'s replay-gain fade uses a wall-clock timer, so the two
  machines' outputs can differ by an inaudible fade-phase amount even when
  perfectly control-synced; documented, accepted, not treated as a bug.
