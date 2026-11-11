// rows_build.h - the answer for one query, built from a snapshot.
//
// Split from rows.h so the row model has no Windows dependency and the tests can
// assert it anywhere; this part reads what the controller already gathered.
#pragma once

#include "controller.h"
#include "rows.h"

#include <string>
#include <vector>

namespace tioplug {

// Builds the rows for one query. `browse` is the list the caller is currently
// showing: None is the main list, anything else a candidate list.
inline std::vector<Row> buildRows(const Snapshot& s, Browse browse, const Lang& lang)
{
    std::vector<Row> rows;

    if (browse != Browse::None) {
        rows.push_back(backRow(lang));

        if (browse == Browse::Ports) {
            if (s.ports.empty()) {
                Row none;
                none.id = "empty";
                none.title = lang.noPorts;
                none.disabled = true;
                rows.push_back(std::move(none));
            }
            for (const PortInfo& info : s.ports) {
                rows.push_back(portRow(info.com,
                    winshun::utf8(info.instanceId), s.port.ok() && s.port.com == info.com, lang));
            }
            return rows;
        }

        if (browse == Browse::Baudrates) {
            int count = 0;
            const int* rates = baudrates(count);
            for (int i = 0; i < count; ++i) {
                rows.push_back(baudRow(rates[i], s.serial.baudrate == rates[i], lang));
            }
            return rows;
        }

        // The diagnostics view: one read-only row per line of the report, so it
        // can be read in the list rather than only copied. The first row also
        // carries the whole report for copying (Shift+Enter). Back comes first,
        // like every other candidate list.
        if (browse == Browse::Diagnostics) {
            rows.push_back(backRow(lang));
            for (Row& row : buildDiagnosticsRows(s.diagnostic, lang))
                rows.push_back(std::move(row));
            return rows;
        }

        rows.push_back(backendRow(Backend::Wt, s.backends.wt, s.backends.wtVersion,
            s.backend == Backend::Wt, lang));
        rows.push_back(backendRow(Backend::Conhost, s.backends.conhost, std::string(),
            s.backend == Backend::Conhost, lang));
        return rows;
    }

    // ---- the main list ----
    const bool active = s.session.active() || s.session.phase == Phase::Validating;
    const bool ready = s.readiness.ready();

    rows.push_back(groupRow("launch", lang.groupLaunch));

    Row main;
    main.id = "launch";
    main.icon = active ? "glyph:U+E71A" : "glyph:U+E768";
    main.title = s.port.ok() ? "tio COM" + std::to_string(s.port.com) : std::string("tio");
    if (s.port.ok()) {
        main.subtitle = "COM" + std::to_string(s.port.com) + lang.arrow + s.port.device()
            + lang.separator + serialText(s.serial) + lang.separator
            + backendLabel(s.backend, lang);
    }
    main.action = active ? "stop" : "launch";
    main.actionKey = "Enter";
    // The standalone window, on the row's menu (Ctrl+Enter): the second way in,
    // visible on the row the user is already on.
    main.moreActions.emplace_back("openWindow", "Ctrl+Enter");
    main.disabled = !active && !ready;
    main.score = 100;
    if (active) {
        // A running session says so, and says the port state exactly as recorded:
        // a started process is not a connected port (TIO-007).
        const char* phase = lang.running;
        if (s.session.phase == Phase::Starting)
            phase = lang.starting;
        else if (s.session.phase == Phase::Stopping)
            phase = lang.stopping;
        main.subtitle = std::string(phase) + lang.separator + main.subtitle;
    } else if (!ready) {
        // Name what is missing rather than leaving a dead row.
        main.subtitle = std::string(lang.blocked) + ": " + gateText(s.readiness, lang);
    }
    rows.push_back(std::move(main));

    rows.push_back(groupRow("parameters", lang.groupParameters));
    const std::string com = s.port.ok() ? "COM" + std::to_string(s.port.com) : "—";
    rows.push_back(parameterRow("port", lang.port, com, "openPorts"));
    rows.push_back(parameterRow("baud", lang.baudrate, std::to_string(s.serial.baudrate), "openBaudrates"));
    rows.push_back(parameterRow("dataBits", lang.dataBits, std::to_string(s.serial.dataBits), "cycleDataBits"));
    rows.push_back(parameterRow("parity", lang.parity, parityName(s.serial.parity), "cycleParity"));
    rows.push_back(parameterRow("stopBits", lang.stopBits, std::to_string(s.serial.stopBits), "cycleStopBits"));
    rows.push_back(parameterRow("flow", lang.flow, flowName(s.serial.flow), "cycleFlow"));
    rows.push_back(parameterRow("backend", lang.backend, backendLabel(s.backend, lang), "openBackends"));

    // The standalone window, as its own row: the visible way in. Ctrl+Enter on
    // the launch row is the other way; this one is for the user who is reading
    // the list rather than the footer.
    {
        Row window;
        window.id = "openWindow";
        window.title = lang.openWindow;
        window.subtitle = lang.openWindowNote;
        window.icon = "glyph:U+E8A9";
        window.action = "openWindow";
        window.actionKey = "Enter";
        window.score = 70;
        rows.push_back(std::move(window));
    }

    rows.push_back(groupRow("information", lang.groupInformation));
    {
        Row control;
        control.id = "control";
        control.title = lang.control;
        control.subtitle = lang.controlNote;
        control.icon = "color:#E8A33D";
        control.disabled = true;
        control.score = 40;
        rows.push_back(std::move(control));

        Row diag;
        diag.id = "diagnostics";
        diag.title = lang.diagnostics;
        diag.subtitle = lang.diagnosticsShow;
        diag.icon = "glyph:U+E9D9";
        diag.action = "diagnostics";
        diag.actionKey = "Enter";
        // The whole report on Shift+Enter, without opening anything.
        diag.copy = s.diagnostic;
        diag.score = 39;
        rows.push_back(std::move(diag));
    }
    return rows;
}

} // namespace tioplug
