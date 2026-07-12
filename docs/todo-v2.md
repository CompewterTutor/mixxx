# Netmix v2 — Cache Encryption & Voice Chat

Deferred from v1 by owner decision (2026-07-12). Do not start until v1 phases
merge to the trunk.

---

## Phase 2.1 — Rotating-Key Track Cache Encryption

- [ ] `2.1.1` Session key exchange — ephemeral X25519 + rotation
  - **Goal:** `src/netmix/sessioncrypto.h/.cpp`: ephemeral X25519 key pair per session (Qt/OpenSSL via already-linked TLS backend); ECDH shared secret; HKDF-derived symmetric keys rotated every N minutes (default 10) with key-id tagging so in-flight data decrypts across one rotation. Keys live only in memory for session duration.
  - **Touches:** `src/netmix/sessioncrypto.h`, `src/netmix/sessioncrypto.cpp`, `src/netmix/protocol.h/.cpp` (KeyRotate message), `CMakeLists.txt`, `src/test/netmixcrypto_test.cpp`
  - **Success:** Both simulated peers derive identical keys; rotation seamless; old keys wiped.
  - **Tests:** `NetmixCryptoTest` — derivation equality, rotation window, wipe.
  - **Difficulty:** High
  - **Model:** Standard

- [ ] `2.1.2` Encrypted-at-rest cache entries (default on)
  - **Goal:** TrackTransfer encrypts chunks with the session key (AES-256-GCM); cache stores ciphertext; decryption only via in-memory session key while the owning peer's session is live — a custom QIODevice/SoundSource shim decrypts on read for playback, nothing plaintext hits disk. Option (default ON) in DlgPrefNetmix; plaintext fallback allowed when disabled by both peers.
  - **Touches:** `src/netmix/tracktransfer.cpp`, `src/netmix/trackcache.h/.cpp`, `src/netmix/cryptoiodevice.h/.cpp`, `src/preferences/dialog/dlgprefnetmix.cpp`, `CMakeLists.txt`, `src/test/netmixcrypto_test.cpp`
  - **Success:** Cached file unreadable without session key; playback works during live session; after session end file cannot be decrypted (key gone) until owner re-shares.
  - **Tests:** Extend `NetmixCryptoTest` — at-rest opacity, live decrypt read path, post-session denial.
  - **Difficulty:** High
  - **Model:** Standard

- [ ] `2.1.3` Phase merge: release/2.1 → feat/rollback-network-mixing
  - **Goal:** Phase review passes, branch merges cleanly into the trunk.
  - **Touches:** todo-v2.md checkboxes
  - **Success:** All 2.1.x tasks checked; review returns PHASE_APPROVED.
  - **Tests:** Full `ctest -R Netmix` suite.
  - **Difficulty:** Low
  - **Model:** Standard

---

## Phase 2.2 — Voice Chat Channel

- [ ] `2.2.1` Mic capture tap + Opus encode
  - **Goal:** `src/netmix/voicechannel.h/.cpp`: tap microphone samples from the `EngineMicrophone` receive path (`src/engine/channels/enginemicrophone.h:30`) via lock-free FIFO (pattern: `ShoutConnection` sidechain); encode with libopus (new optional CMake dependency, feature-gated `NETMIX_VOICE`) at 48 kHz mono 32 kbps.
  - **Touches:** `src/netmix/voicechannel.h`, `src/netmix/voicechannel.cpp`, `CMakeLists.txt` (opus find_package, option), `src/test/netmixvoice_test.cpp`
  - **Success:** FIFO tap adds no locks/allocs to audio callback; encoded frames produced at steady cadence.
  - **Tests:** `NetmixVoiceTest` — encode round-trip with decoder, FIFO overflow behavior.
  - **Difficulty:** High
  - **Model:** Standard

- [ ] `2.2.2` Voice transport + playback mix-in
  - **Goal:** Opus frames over dedicated UDP stream (separate sequence space, jitter buffer 40–80 ms adaptive); decode and mix into a new auxiliary-style output path (pattern: `EngineAux`) with its own volume CO `[Netmix],voice_volume` and talkover-style ducking option.
  - **Touches:** `src/netmix/voicechannel.h/.cpp`, `src/netmix/udpchannel.cpp`, `src/netmix/netmixsessionmanager.cpp`, `CMakeLists.txt`, `src/test/netmixvoice_test.cpp`
  - **Success:** Loopback voice path end-to-end with simulated jitter stays glitch-free within buffer bounds; volume/duck COs effective.
  - **Tests:** Extend `NetmixVoiceTest` — jitter buffer under drop/reorder scripts.
  - **Difficulty:** High
  - **Model:** Standard

- [ ] `2.2.3` Phase merge: release/2.2 → feat/rollback-network-mixing
  - **Goal:** Phase review passes, branch merges cleanly into the trunk. v2 complete.
  - **Touches:** todo-v2.md checkboxes
  - **Success:** All 2.2.x tasks checked; review returns PHASE_APPROVED.
  - **Tests:** Full `ctest -R Netmix` suite.
  - **Difficulty:** Low
  - **Model:** Standard
