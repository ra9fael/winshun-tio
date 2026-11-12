# tio launcher UI specification

[中文](UI-SPEC.md) · [Index](README.en.md) · [Requirements](plugin-design.en.md) · [Tests](TEST-PLAN.en.md)

Status: **both path A (the search box) and path B (the standalone QML window) are implemented**; see the [implementation notes](docs/implementation.en.md). The ASCII drawings below remain non-programmatic illustrations. Requirement IDs/priorities/observable acceptance are in design section 1. This window configures and manages upstream tio without displaying or parsing serial terminal content.

## Pages and wireframes

TIO-001/004/009/011: Enter on the host `tio` row opens the single launcher window; only the user's explicit Launch action opens a port.

```text
+ tio launcher ----------------------------------- [Close] +
| Port [COM10________ v] [Refresh ports]                    |
| Mapping /dev/ttyS9     Port dropdown = Device Manager name (auto) |
| Baud [____] Data bits [v] Parity [v] Stop bits [v]        |
| Flow control [________ v]                                |
| Lines: locked tio version ____ / default or saved setup |
| Actual DTR/RTS defaults [fill after version verification] |
| Startup/exit transients, reset effects [Info] [Advanced] |
| Backend [Pending probe selection v] [Diagnostics]        |
| Package version/architecture ___ Integrity [Not verified]|
| Error/field explanation ______________________________  |
| [Save settings] [Configuring] [Launch: when valid] [Stop] |
+---------------------------------------------------------+
```

First configuration clearly displays the locked tio version's verified actual serial and DTR/RTS defaults, including levels, flow control, transients during port opening/operation/normal stop/abnormal exit and possible reset effects. Save settings in dataDir with the package version. No version is locked or measured yet: do not fill in concrete defaults or invent CLI switches. Later launches reuse settings through the user's Launch action, without repeated DTR/RTS choices, a line-confirmation checkbox or a launch-confirmation panel. Real waveform measurements for defaults and every offered mode remain a delivery gate.

Users explicitly open advanced line settings to change verified supported modes, with Save changes/Cancel; saves affect future sessions only. Explain automatically managed RTS without promising a fixed level. Unsupported or behaviorally unknown modes are unavailable with reasons; never label unknown behavior “Hold/Safe”. “Hold” requires verified capability and actual waveforms. Package updates recheck defaults, setting compatibility and waveforms and update displayed information; reuse compatible settings without normal-launch confirmation.

TIO-004/007/009/010: the following summary appears inline, without an extra launch-confirmation panel.

```text
+ Current settings summary (inline) ----------------------+
| COM10 -> /dev/ttyS9                                      |
| Current identity ____  Verified at ____                 |
| Baud/data/parity/stop/flow ____                          |
| DTR ____ / RTS ____ (startup/exit behavior explained)    |
| Backend ____ / Version ____                             |
| Host lifecycle applies; host exit ends owned sessions   |
| Unknown identity: current port only, physical identity  |
| cannot be guaranteed                                   |
|                                      [Edit] [Launch]   |
+---------------------------------------------------------+
```

TIO-007/008/013: running status does not fabricate connection status. The selected backend owns the terminal window.

```text
+ Session status -----------------------------------------+
| Session ID ____  COM10 -> /dev/ttyS9                     |
| Process: started / Port: unknown (or verified open)     |
| Backend ____ Started ____ Redacted diagnostics [Show]  |
| [Focus terminal: only if verified] [Stop this session]  |
| Error: busy / denied / device removed / backend exited  |
| [Reconfigure] [Explicit retry: revalidate port identity]   |
+---------------------------------------------------------+
```

## Components, fields and validation

| Component | Constraints/states | Requirements |
|---|---|---|
| Port input/list | Positive COMn with n-1 mapping; distinguish empty list, invalid input and removal; parsing differs from actual existence | TIO-001/004/010 |
| Identity | The port dropdown shows the Device Manager friendly name; picking a port recaptures the identity automatically, with no button; the "Device details" card starts folded and shows manufacturer, product, VID/PID, serial, bus, driver, location, instance id and problem code, with the capture time in its header | TIO-010/013 |
| Multi-session | Tabbed: each tab binds one port and at most one process; one port never carries two sessions; Launch/Stop act on the focused tab; host shutdown stops all; the search box lists other sessions row by row and stops one row at a time | TIO-007/008/013 |
| Package integrity | A summary line (tio version · runtime file count · size, all from the manifest) plus the third-party record inside the card (component, version, licence, source link, in-package licence text, why it is in the package; not-bundled items are marked); THIRD_PARTY.md and manifest/files.json stay reachable as the appendix | TIO-002/003 |
| Serial form | Baud/data/parity/stop/flow values supported by locked version; inline errors preserve other inputs | TIO-011 |
| DTR/RTS | Show actual defaults on first configuration and save; reuse thereafter; explicit advanced changes only, unsupported/unknown modes unavailable, no per-launch confirmation | TIO-009 |
| Backend | Unprobed/verified available/missing/incompatible; no automatic download | TIO-005 |
| Integrity | Unverified/validating/valid/missing/wrong architecture/hash failure; name failed component, no port open | TIO-002/003 |
| Launch/stop | Configuring/validating/starting/running/stopping/exited/failed/device removed; prevent duplicate actions | TIO-007/008/013 |
| Diagnostics | Upstream/package/backend versions, exit reason, owned process state, redacted paths; no serial content | TIO-012/013 |
| Integration | Currently unverified; missing device never blocks manual port input | TIO-001/014 |

Launch requires valid parameters, compatible settings, integrity, backend and current identity, with no per-launch line-choice or confirmation condition. Reread identity immediately before launch; changes return to port selection and invalidate the old identity snapshot. Reselection retains saved line settings and never replays launch. Handle unknown or changed identity under TIO-010. Busy/denied never triggers automatic retry. No driver installation, forced port takeover, automatic transmit, reset or arbitrary CLI text box.

## Keyboard and focus

TIO-011: Tab order is port → refresh → identity → baud → data → parity → stop → flow → line information → advanced settings → backend → diagnostics → save settings → launch → stop. Advanced settings use DTR → RTS → cancel → save changes; arrows select combo values; data/parity/stop/flow are segmented radios (a radio group): Tab stops at the first segment and the arrows move the choice. Enter launches only while explicitly focused on Launch; in port input it validates only. Esc closes the topmost panel, or closes an idle window; active sessions show close choices. A focusable error summary links to fields. Updates/hotplug never steal focus. Closing dialogs returns to the trigger; closing the window returns to the host. Terminal keys belong to upstream/backend; the plugin does not intercept serial transmit keys or forcibly terminate via global hotkeys.

## Scaling, themes and accessibility

TIO-011: compact, modern, simple launcher aligned with host appearance language/light-dark/corners/size, with system fallback for missing fields and ignored unknown fields. Backend theme support requires probing; automatic host-to-terminal theme synchronization is not promised. Controls remain reachable at 100%–200% DPI, across monitors and in narrow windows; grouped forms wrap, diagnostics scroll and long Unicode paths remain selectable. Candidate logical minimum size is 640×480, awaiting L4 calibration.

Expose labels/values/error relationships to screen readers and announce state changes politely. Explain disabled Launch to keyboard and screen-reader users. Show focus, pair color with words and support high contrast/reduced motion. Candidate contrast targets are ≥4.5:1 for normal text and ≥3:1 for controls/focus, unmeasured and not a compliance claim. Backend selection must separately document actual terminal keyboard/Unicode/screen-reader behavior; launcher accessibility cannot substitute for terminal accessibility.

## Window and process lifecycle

TIO-007/010: one launcher window per process and at most one owned active session per port. Repeated launch must not create another tree. Closing with active sessions offers Continue in background / Stop session and close / Cancel. Background hides the launcher while preserving owned-session/host-Job relationships. Stop prevents duplicates, attempts graceful termination and reclaims only the owned tree after timeout; other user terminals survive. Terminal close updates exit state. Backends with unclear broker ownership are ineligible.

Disable/uninstall/host exit/launcher crash must reclaim owned sessions. Normal shutdown cleans before replying; crash cleanup relies on the Job and requires L3/L4 validation. Lock/unlock never auto-reconnects; revalidate ports on resume. Removal immediately invalidates/stops the owned session; another device at the same COM number cannot inherit it. Restart retains saved settings without replaying launch; port identity is independently revalidated. None of these behaviors has been verified.
