# WinShun MCP `tio-io` Validation

Validated on 2026-11-25 through the deployed and running WinShun MCP paths:

- Host: `WinShun/build/release/wsbench.exe --mcp-host`
- Host Qt directory: `C:\Program Files\WinShun`
- Plugin deployment: `build/mcp-tio/ra9fael.tio`
- Relay: `C:\Program Files\WinShun\WinShunMcp.exe`
- Transport: named pipe `WinShun.Mcp.tio-verify`
- Running-instance transport: the user's default per-user WinShun MCP pipe

Results:

- `wsbench` served MCP with the complete `WS_APP_DIR` value `C:\Program Files\WinShun`.
- The plugin loaded `tio.exe` through `WinShunPluginHost.exe` without a Qt runtime error.
- `tools/list` exposed `plugin_ra9fael_tio_tio_io` and `plugin_ra9fael_tio_tio_io_run`.
- `status` returned `No user-shared session is running`.
- `read COM9 --format hex --max 16` returned `COM9 is not a user-shared session`.
- `send COM9 --format ascii --data AT` returned `COM9 is not a user-shared session`.
- No serial session was opened and no bytes were sent.
- The rebuilt local suite passed `tio_logic`, `tio_window_model`, `tio_share_client`, `tio_ansi_text`, and `tio_share_port_preflight`.
- The protocol integration suite passed all 61 checks without launching a serial session.
- The current development package was installed into the user's plugin directory; its SHA-256 is `87e177038378b67623df9cd4d0bd7c94eee950ff138e12d6ec515c67b53cb661`.
- The running WinShun instance then exposed `plugin_ra9fael_tio_tio_io` and `plugin_ra9fael_tio_tio_io_run` through its default MCP pipe.
- A user-started `COM10` session was observed as `connected port=54321`.
- The MCP query returned a send plan for `WSMCP-234014866`; `_run` accepted it and `read COM10 --format hex` returned the exact loopback bytes.
- After stopping and restarting the shared session, the session token changed. An `_run` using the old token was rejected, while a new `WSMCP-RECONNECT-234859543` send was accepted and echoed back.
- The shared client configures a 500 ms `SO_SNDTIMEO` and writes in 64 KiB chunks. The focused shared-client test passed 30/30 checks: a non-reading peer caused a failed write after 512 ms, and a disconnected peer caused a failed write after 402 ms.

The initial absent-session results are expected and confirm that the plugin does not open a port or enable sharing automatically. Real shared-session byte reads, writes, loopback, and session-generation rejection are validated above. Disconnect and blocked-write behavior is covered by the focused local socket test; the user session was not used as a destructive timeout fixture.
