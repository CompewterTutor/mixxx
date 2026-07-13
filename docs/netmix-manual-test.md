# Netmix Manual Smoke Tests

## 1.7.1 DlgNetmixConnect Dialog

### Open dialog from menu
1. Launch Mixxx.
2. Click Options → **Netmix Connect...**
3. **Expected:** The Netmix Connect dialog opens with Host radio selected, port 21200, display name empty, all deck combos at "Open", Connect button enabled, Disconnect button disabled, status "Idle", RTT "-- ms".

### Toggle Host/Join
1. Click **Join** radio.
2. **Expected:** Peer IP field remains editable. Port field remains editable.
3. Click **Host** radio.
4. **Expected:** Peer IP field still editable (port is what matters for host).

### Set display name and connect
1. Enter a display name (e.g. "MyMixxx").
2. Select pre-assignment: Channel 1 = "Local", Channel 2 = "Remote", Channels 3-4 = "Open".
3. Click **Connect**.
4. **Expected:** Status changes to "Connecting..." then (if a peer connects) "Connected". All input fields become disabled. Disconnect button enabled. Connect button disabled.

### Disconnect
1. Click **Disconnect**.
2. **Expected:** Status returns to "Idle". All input fields re-enabled. Connect button enabled. Disconnect button disabled. RTT resets to "-- ms".

### Invalid IP handling
1. Switch to Join mode, enter invalid IP like "notanip".
2. Click **Connect**.
3. **Expected:** Status shows "Invalid peer IP address". No crash.

### Dialog reopen
1. Close the dialog.
2. Open it again from Options → **Netmix Connect...**.
3. **Expected:** Dialog shows same state (Idle, inputs editable, previous values may or may not persist).
