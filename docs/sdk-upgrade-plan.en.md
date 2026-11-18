# tio: shared configuration plan

[中文](sdk-upgrade-plan.md)

## Why change

The search list and standalone window already share one Controller. However, `controller.h` prefers every value recorded in `tio.json` over host settings, and `plugin.h::settingsChanged()` only seeds values that were never recorded. Once the user edits a value in the plugin, changing that value on the host card can cease to have an effect.

The existing window already handles complete serial configuration and per-session work. It should be reused as the detailed configuration entry, while the search interface emphasizes choosing a port and starting or stopping its session.

## Settings and interface split

| Surface | Contents |
|---|---|
| Search home | One launch/stop row per port, running sessions, baud-rate shortcut, current configuration summary and window entry |
| Host card | Default port, default baud rate and terminal backend preference |
| Standalone window | Complete serial parameters, per-session settings, remote sharing, sharing port, diagnostics and default configuration controls |

Other existing parameter lists can remain accessible under one configuration entry rather than occupying search home.

Default port, baud rate and backend preference are host-owned. Data bits, parity, stop bits, flow control and sharing defaults become plugin-owned advanced settings when their manifest declarations are removed. Per-session forms, running process state, UI state and drafts remain local.

## Implementation

1. Update the SDK and add the outbound settings callback to the shared configuration layer. Both search actions and window saves use it.
2. Replace unconditional persisted-value precedence for host-owned keys. Initialize from the host; apply later `settingsChanged()` values to saved defaults. Send only user-changed host keys outward.
3. Preserve backend preference separately from the currently resolved executable. Saving a different field must not change `auto` into `wt` merely because Windows Terminal is installed.
4. Transfer existing serial and sharing defaults before removing advanced manifest keys. Retain the local file for those settings, package information and UI state.
5. Keep default configuration, session forms and running-session parameters distinct. Updating defaults affects future sessions; it does not reconnect, restart or reconfigure an active session.
6. A clean default form follows incoming saved changes. A dirty form retains user edits; saving applies its edited fields rather than an obsolete full snapshot. Existing per-session forms retain their session identity and parameter semantics.
7. Add `settingsWindow: true` and `showSettings()` to open/focus the existing window's configuration area. Opening settings does not open a serial port. The search window action remains the cold-start path for this lazy plugin.
8. Preserve the search-launch rule that sharing is disabled. Window-launched sharing retains its existing explicit access disclosure and confirmation behavior.

Baud rate uses common choices plus a custom positive integer. Do not convert it into a linear slider. Sharing port uses an exact integer input constrained to the existing 1024–65535 range. Short serial enums use selection controls with localized titles.

## Acceptance

- A host baud-rate edit takes effect even when `tio.json` already contains an older value.
- Saving a default port or baud rate from either plugin surface updates the host card and survives restart.
- Choice settings are serialized with the manifest's string values where host-owned.
- Backend preference remains `auto` across unrelated saves.
- Updating defaults does not stop/start a session or overwrite an unsaved form.
- Search launches remain local-only; settings changes never implicitly enable a sharing session.
- Existing configuration and window tests cover host updates, edited-field saves and persistence using isolated data; protocol checks do not open real serial hardware.

## Documentation

Remove the description that host settings are merely first-use defaults. Explain shared defaults, per-session configurations, what requires restart and where advanced configuration is edited. Keep hardware validation status accurate and separate from this configuration work.
