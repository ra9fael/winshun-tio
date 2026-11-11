# tio launcher design and requirements

[中文](plugin-design.md) · [Index](README.en.md) · [UI](UI-SPEC.en.md) · [Tests](TEST-PLAN.en.md)

## 1. Verdict

Package and launch upstream tio with traceable provenance; do not implement a serial terminal. **COMn → /dev/ttyS(n-1)** is settled and needs no further justification. Users need no MSYS2 installation. Select an external terminal or mintty only after real-host and clean-machine probes: starting an exe does not establish interaction or lifecycle correctness. The first version is implemented: both launch paths (the search box and the standalone QML window) share one state. The checks that have not run are listed in [validation](docs/validation.en.md). P0 means core required, P1 first-delivery required, P2 optional. Stable IDs must never be reused.

| ID | Priority | Requirement and observable acceptance |
|---|---|---|
| TIO-001 | P0 | Role/independence: standalone installation and own COM enumeration/input; missing device does not affect launch; no custom terminal rendering, serial traffic parsing or driver modification. |
| TIO-002 | P0 | Runtime closure: extract tio.exe and required recursive/dynamic DLL dependencies from locked MSYS2 packages; run on clean Windows without MSYS2/bash/pacman/developer PATH; missing/wrong-architecture components are reported before connection. |
| TIO-003 | P0 | Versions/integrity: record package, upstream/package versions, repository, architecture, source package, acquisition verification and per-file SHA256; block tampered/mixed/missing components without silent online replacement. |
| TIO-004 | P0 | Mapping/arguments: map COM1/10/256 using n-1; reject COM0, negative/noninteger/overflow values and injection; structured arguments preserve spaces/Unicode paths without shell concatenation. |
| TIO-005 | P0 | Terminal backend: probe external terminal/mintty TTY, keyboard, Unicode, exit status and process/window ownership before selection; no backend gives an actionable error, no implicit installation or invented connection success. |
| TIO-006 | P0 | processes/ui and privileges: normal only, declare processes/ui; no elevated/UAC/driver operations; no child processes/windows before permission consent. |
| TIO-007 | P0 | Lifecycle: unique session ID, launch/exit/failure and owned process tree; close/cancel/disable/crash/host exit reclaim owned children/handles without killing other user terminals or breaking away from Jobs. |
| TIO-008 | P0 | Occupied ports: do not pre-open ports to probe availability; report actual tio busy/denied errors with explicit retry; never kill the owner or reconnect automatically. |
| TIO-009 | P0 | DTR/RTS: on first configuration clearly show the locked tio version's verified actual default levels, flow control and startup/exit behavior, then save version-associated settings. Reuse settings for normal launch without repeated choices or extra confirmation. Advanced line settings change only by explicit user action; verify version capabilities and real waveforms, block unsupported custom modes, never label unknown behavior “Hold/Safe”; missing measurements block delivery. |
| TIO-010 | P0 | Hotplug safety: revalidate port and readable identity immediately before launch; removal or COM reuse invalidates/stops the session; no reconnection by port name; require reselection/confirmation. |
| TIO-011 | P1 | Accessible form: validate baud, data bits, parity, stop bits, flow control, control lines and backend; support keyboard/DPI/themes/screen readers; no arbitrary shell or unaudited advanced arguments. |
| TIO-012 | P0 | Licensing/provenance: verify license/source/notice/redistribution duties separately for tio, MSYS2 runtime, every DLL, bundled mintty and launcher/UI dependencies; any required unresolved item blocks delivery; a source URL alone does not establish compliance. |
| TIO-013 | P1 | Data/diagnostics: no serial-content logging by default; settings in dataDir, redacted identity/path logs; distinguish started/port opened/unknown without unsupported connection claims; performance remains candidate targets. |
| TIO-014 | P2 | Optional integration: require generic invocation evidence, available target and explicit user action before accepting device entries; currently unverified, with no dependency, invented protocol or automatic connection. |

## 2. Grounded facts

Only local workbench rules and necessary protocol/source files were inspected. No MSYS2 downloads, tio execution, serial enumeration or probes occurred.

| Evidence | Inspected fact | Limit |
|---|---|---|
| [Workbench rules:40](../DEVELOPMENT.md) | Eleven sections, bilingual docs, L0–L5 and L3 gate | Not implementation approval |
| [Resident protocol:226](../WinShun/docs/plugins.md) | Stdio JSON-RPC, settings, cancellation/shutdown | tio stdout must not enter plugin protocol |
| [Permissions:402](../WinShun/docs/plugins.md) | processes enables children; ui supports own windows; normal privileges | Interactive terminal behavior inside Jobs untested |
| [Runner:495](../WinShun/src/plugin/runner/main.cpp) | Reads processes/ui and creates Job | Not terminal compatibility evidence |
| [Sandbox.cpp:219](../WinShun/src/plugin/runner/Sandbox.cpp) | KILL_ON_JOB_CLOSE; ActiveProcessLimit=1 without processes | Actual ownership/broker behavior requires verification |
| [Window protocol:269](../WinShun/docs/plugins.md) | Resident+ui, appearance, settingsWindow | UI technology undecided |
| [Session.cpp:115](../WinShun/src/plugin/Session.cpp), [host tools:265](../WinShun/docs/plugins.md) | winshun/tool dispatch and listed tools | No generic plugin invocation evidence or established device→tio API |
| [Schema:75](../WinShun-plugins/schema/plugin.schema.json) | Runtime, permission/settings fields | Actual Manifest.cpp parser and minimum version remain unverified |

User-established decisions: COMn→/dev/ttyS(n-1), extracted MSYS2 runtime packaging, no MSYS2 requirement on clean machines, and no custom terminal. No specific upstream package license was inspected; no SPDX identifier, version or DLL inventory is claimed as settled.

## 3. Architecture and textual manifest draft

Candidate native resident launcher: protocol thread, UI thread, package-integrity/port-identity worker and session manager. The pure core handles COM grammar/mapping, form validation, argument models, integrity inventory comparisons and state transitions. Windows adapters provide read-only enumeration, process/Job/handle operations, terminal launch and removal events. Upstream tio owns serial I/O; the terminal backend owns display/keyboard. The launcher does not read and render serial bytes.

Future package responsibilities: plugin.json; launcher under bin; runtime/tio.exe and necessary DLLs; optional terminal/mintty runtime; licenses/component notices; sources/compliance materials or appropriate acquisition instructions; version/hash inventory. This is textual planning, not files/directories created this round. Start DLL discovery with recursive PE imports, then delay-load, dynamic loading and runtime traces. Distinguish Windows system DLLs from redistributable dependencies. Do not copy all MSYS2 or prune from one successful startup. Use private PATH/cwd, prevent unrelated MSYS runtimes from being loaded, do not change user/system PATH or load code from dataDir.

Textual manifest draft: manifest=1, id=ra9fael.tio, bilingual name/description/commands/settings; version/license/minWinShun undecided. Resident, package-local exe, normal, lazy; permissions processes/ui, with files:write declared for the final explicit own-settings/diagnostic write plan; no network/elevated. Stable command tio, candidate keyword tio; actions configure, launch, stop, diagnostics. Queries show candidates without opening ports; users explicitly activate Launch using saved settings, without an additional normal-launch confirmation panel. initialize obtains dataDir/settings/appearance; settingsChanged affects future sessions only; shutdown stops owned sessions and waits for cleanup before replying. Keep child stdio entirely separate from protocol stdout; stderr contains redacted diagnostics only.

Calibrate timeoutMs/memoryMB/idleExitSec after probes. Active sessions must not be interrupted by ordinary idle cleanup; candidate idleExitSec=0 with explicit lifecycle control. External terminals may forward to existing brokers; mintty may need additional runtime. Probe ownership, interaction and cleanup before choosing. Backends requiring breakaway or uncontrollable user-process reuse are ineligible for the first version. No eligible backend blocks delivery; it does not justify building a terminal.

## 4. Core logic

TIO-004: accept case-insensitive COM names and display canonical COMn. n is a positive decimal integer; range-check conversion and n-1 arithmetic. COM10→/dev/ttyS9 and COM256→/dev/ttyS255. Parsing does not establish that a high-numbered port exists; report absence without changing the mapping. Define encoding boundaries for Windows paths/Unicode; build argv with correct Windows quoting, never concatenate into cmd/bash or accept arbitrary shell arguments.

TIO-002/003: lock exact package version/revision/architecture and source/verification metadata. Track per-file provenance, hashes and evidence explaining inclusion. Verify required components before offline launch. Combine inventories with package SHA256 and trusted distribution provenance; a locally mutable hash list is not authentication. Trace startup, help, real interaction, exit and failure paths for dynamic dependencies. Retest on clean machines without developer PATH. Every update recomputes closure, obligations and compatibility; no mixed versions or first-run downloads.

TIO-008/009/010: enumerate read-only without opening the port exclusively. Immediately before connection, reread port→InstanceID/interface/readable identity and compare to the confirmed snapshot. If identity is unreliable, disclose that only the current port can be confirmed and require another confirmation. Removal or identity change invalidates the session, stops owned processes and prevents retry against a reused COM number. Process exit does not prove hardware reset or absence of transient control-line changes.

Allow only parameters supported by the locked tio version: supported positive baud rates and supported data-bit/parity/stop-bit/flow-control enums. First configuration clearly displays that version's verified actual default DTR/RTS behavior, including levels and transitions during port opening, operation, normal stop and abnormal exit, flow-control management and possible reset effects. Save it with serial settings in dataDir, associated with the package version. Subsequent normal launches reuse settings through the user's Launch action without repeated DTR/RTS choices or extra confirmation. Advanced line settings allow only explicit user changes to verified supported modes, with no invented CLI switches; flow-managed RTS must not also promise a fixed level. Unsupported or behaviorally unknown custom modes are unavailable with a reason; unknown behavior must never be labeled “Hold/Safe”. Measure the default and every offered advanced mode on real hardware across all phases above. Disclose every transient/pulse; missing measurements or mismatched documentation block delivery, and repeated user confirmation cannot replace measurement. Package updates require rechecking defaults, setting compatibility and waveforms, and updating displayed version/behavior information; reuse compatible settings without adding a normal-launch confirmation gate. Automatic reconnect, startup transmit strings, break and reset pulses are absent by default and require separate requirements/approval if added. Users operate serial traffic through upstream tio; the plugin itself sends no automatic data and makes no driver changes.

TIO-007/013: session states are configuring→validating→starting→running (port status may be unknown)→stopping→exited/failed/device removed. Process creation establishes “Started”, not “Port opened”; only a verified signal from the locked upstream version permits the latter label. Partial creation/Job/handle failures reclaim created resources. Track owned PIDs with creation times/handles against PID reuse; never kill by process name. After graceful-stop failure, terminate only the owned session subtree; host Job cleanup is the final safeguard. Do not enable breakaway.

## 5. Interaction

See [UI specification](UI-SPEC.en.md). A `tio` query offers selection/configuration only. Users enter COMn or select from a read-only list. Show backend, serial parameters, identity timestamp, the locked version's actual default DTR/RTS behavior or saved advanced settings. Show final COM/mapped path, line behavior and process-lifecycle information inline. Save first configuration and reuse it on subsequent launches without a separate normal-launch confirmation panel. No terminal, busy, identity change and hash failure have distinct states/remedies. Stop affects owned sessions only. Closing the launcher offers Continue in background / Stop session and close / Cancel; background operation remains subject to host lifecycle. Duplicate actions focus existing session state rather than launching duplicates.

## 6. Permissions, data and privacy

Normal+processes+ui is the baseline without elevation; report denied serial access. processes covers tio/terminal children, ui covers form/terminal windows. Normal filesystem/network permissions are not enforced isolation and must be described accurately. Save settings in host dataDir, with final files:write declaration matched to writes. Do not store serial content, raw hardware identity or personal absolute paths in default logs. No automatic network access.

TIO-012 requires the following component-by-component review. Package metadata and an “open source” label cannot replace original terms. Each actual DLL needs its own row rather than a collective runtime entry. With no package selected, this is a future verification template, not a passing compliance statement.

| Object | Required verification/evidence | Current status/gate |
|---|---|---|
| tio.exe | Exact version, original license, copyright/NOTICE, binary redistribution, matching source/acquisition, modifications | Unverified; close before distribution |
| MSYS2 runtime | Selected package/file terms and exceptions, corresponding source/build-material duties, availability/delivery method | Unverified; ordinary repository URL alone is insufficient |
| Every required DLL | Separate version/source/hash, original license/attribution, source/notice/linking or replaceability conditions if applicable | Each unverified; any missing item blocks delivery |
| Bundled mintty | Exact version, terms, corresponding source/modifications/redistribution and its DLL closure | Unverified; record rationale if not distributed |
| External unbundled terminal | Discovery/invocation and usage limits; recheck all obligations if later bundled | Unverified; local presence is not redistribution permission |
| Launcher, SDK, UI dependencies | Own license choice, third-party notices, static/dynamic linking and source/relinking conditions if applicable | Unverified; review each component |
| Entire .wsplugin | Readable notices/original licenses; fulfill corresponding-source/applicable written-offer terms; source matches binaries | Close all L0/L5 items before release |

## 7. Failure matrix

| Condition | Observable behavior | Recovery/degradation | Requirements |
|---|---|---|---|
| Missing/wrong-architecture DLL or hash mismatch | Validation fails, no port open | Name component/reinstall remedy; no automatic download | TIO-002/003 |
| Missing terminal or incompatible TTY/Job | No usable backend | Choose another verified backend; otherwise block launch | TIO-005/007 |
| Spaces/Unicode/injection-like parameters | Valid paths work; invalid inputs fail validation | Retain input and identify field; no shell | TIO-004/011 |
| Busy/denied/absent port | Distinct reason, not connected | Explicit retry after user resolves issue; never kill owner | TIO-008 |
| Unsupported/unknown advanced line mode | Mode unavailable with reason; no “Hold/Safe” label | Reuse verified saved settings; unmeasured defaults/offered modes block delivery, without per-launch confirmation | TIO-009 |
| Removal/COM reuse/uncertain identity | Device removed or confirmation required | Stop owned session, reselect, never auto-reconnect | TIO-010 |
| Partial launch/immediate terminal exit | Launch failure with exit information | Reclaim owned partial processes/handles | TIO-007/013 |
| Host disable/crash/exit | Session stops/cleans up | Job safeguard, no orphans or unrelated terminal termination | TIO-007 |
| Missing device/unknown invocation | Integration unverified/unavailable | Own port selection remains usable | TIO-001/014 |
| Any open license/source obligation | Delivery blocked | Complete materials or remove component and reverify closure | TIO-012 |

## 8. Delivery checklist

This round delivers eight bilingual requirements documents only; no implementation, scripts, configuration or commits. Future delivery includes bilingual README/CHANGELOG/release notes, manifest, independent .wsplugin/SHA256, locked package/file provenance, tio and minimal verified DLL closure, selected terminal approach, per-component licenses/source compliance materials, parameter/control-line guidance, cleanup policy and evidence. Architecture and minimum Windows/host versions await probes; do not invent supported versions. No plugin-library release before L3/L4 and compliance gates pass.

## 9. Test plan L0–L5

See [test cases](TEST-PLAN.en.md). L0 checks package/manifest/hashes/licenses; L1 portable COM/argv/parameters/state machine; L2 fake OS/terminal/process/identity with faults; L3 real normal host processes/ui, protocol and Job round trips, mandatory for completion; L4 unlocked desktop with real serial hardware, control-line measurements, terminal input and hotplug, mandatory for first delivery; L5 clean-machine closure, calibration and release review. All cases are not executed.

Candidate interaction-latency targets awaiting measurement: cached-query p95 ≤100 ms; Launch click to interactive terminal p95 ≤2 s; owned subtree exit ≤2 s after stop. None has passed. Do not preset a working-set cap. Measure launcher idle/steady baseline B and peak P across launch, active sessions, diagnostics, stop and failure cleanup; derive the budget as B + (P−B) + justified headroom M, recording measurements, scenarios, growth trends and headroom rationale before calibration. Separate launcher, tio, terminal and external broker measurements and report the overall session peak without masking lifecycle failures. Record hardware/OS/host/backend versions, Defender, cold/warm conditions and sampling methods. Choose repetitions from scenario coverage, variability and p95 estimation precision; report actual sample counts and uncertainty without an arbitrary fixed count.

## 10. Honest notes

Measured: no runtime tests; only section 2 local files were inspected, and the initial repository contained Git metadata only. The user settled COM mapping and product boundaries. Inferred: resident normal+processes/ui can support a launcher; current Job constraints support intended cleanup. Unverified: MSYS2/tio versions, DLL/dynamic closure, every license/source duty, backend, actual DTR/RTS waveforms, busy errors, real high-numbered COM ports, Unicode/spaces, host/broker cleanup, UI/minimum versions, performance and cross-plugin invocation. Static inspection is not Windows validation.

## 11. Post-implementation addendum

Recorded in [validation](docs/validation.en.md): L0 (215 assertions), L1 (30 real-machine checks), L2 (49 protocol round trips) and L2' (13 window checks) pass; L3 reached the protocol only; L4 (real serial hardware, DTR/RTS waveform) and L5 (clean-machine closure, performance calibration) have not run. That file is the record of what was measured, what is inferred, and what blocks delivery.
