// rows_build.h - the answer for one query, built from a snapshot.
//
// Split from rows.h so the row model has no Windows dependency and the tests can
// assert it anywhere; this part reads what the controller already gathered.
#pragma once

#include "controller.h"
#include "rows.h"

#include <algorithm>
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

        // The baud list: the usual rates, plus the current one when the user set
        // a custom value, so what is on screen is always one of the rows.
        for (const int rate : baudrateChoices(s.serial.baudrate))
            rows.push_back(baudRow(rate, s.serial.baudrate == rate, lang));
        return rows;
    }

    // ---- the main list ----
    // One row per serial port, then the baud shortcuts and the window entry.
    // No group captions: every row says what it does on its own, and the list
    // stays short enough to read at a glance.
    std::vector<int> ports;
    for (const PortInfo& info : s.ports)
        ports.push_back(info.com);
    // A snapshot without an enumerated port list (a headless check, a machine
    // where enumeration failed) still offers the configured port.
    if (ports.empty() && s.port.ok())
        ports.push_back(s.port.com);

    if (ports.empty()) {
        Row none;
        none.id = "launch";
        none.icon = "glyph:U+E768";
        none.title = "tio";
        none.subtitle = lang.noPorts;
        none.disabled = true;
        none.score = 100;
        rows.push_back(std::move(none));
    }

    // The gates no other port can fix: a broken package, an unusable parameter
    // set, no terminal backend. The port-specific gates (port, portBusy,
    // identity) belong to one port, and the session gate to the tab that holds
    // it, so those are named by the launch attempt's toast instead of greying
    // every row: a session running on COM8 must not block COM9.
    Readiness shared;
    for (const Gate gate : s.defaultReadiness.blocking) {
        if (gate != Gate::Port && gate != Gate::PortBusy && gate != Gate::Identity
            && gate != Gate::Session)
            shared.blocking.push_back(gate);
    }

    for (const int com : ports) {
        const SessionView* view = nullptr;
        for (const SessionView& candidate : s.sessions) {
            if (candidate.form.parsed().com == com)
                view = &candidate;
        }
        const bool active = view && (view->session.active()
            || view->session.phase == Phase::Validating);

        Row row;
        row.id = "port:" + std::to_string(com);
        row.icon = active ? "glyph:U+E71A" : "glyph:U+E768";
        row.title = "tio COM" + std::to_string(com);
        // The mapping is what tio itself will open; the serial parameters and the
        // backend are the same for every row, so they live on the config row.
        row.subtitle = "COM" + std::to_string(com) + lang.arrow + Port::devicePath(com - 1);
        // A running session is what the row stops; the port only changes when
        // the row starts it.
        row.action = active ? "stopPort" : "launchPort";
        row.actionKey = "Enter";
        // The standalone window, on the row's menu (Ctrl+Enter): the second way
        // in, visible on the row the user is already on.
        row.moreActions.emplace_back("openWindow", "Ctrl+Enter");
        row.score = 100;
        if (active) {
            // A running session says so, and says the port state exactly as
            // recorded: a started process is not a connected port (TIO-007).
            const char* phase = lang.running;
            if (view->session.phase == Phase::Starting)
                phase = lang.starting;
            else if (view->session.phase == Phase::Stopping)
                phase = lang.stopping;
            row.subtitle = std::string(phase) + lang.separator + row.subtitle;
        } else if (!shared.ready()) {
            // Name what is missing rather than leaving a dead row.
            row.subtitle = std::string(lang.blocked) + ": " + gateText(shared, lang);
            row.disabled = true;
        }
        rows.push_back(std::move(row));
    }

    // Every session the window opened gets its own row: the search box stays
    // honest about processes that are out there while the user works here.
    for (const SessionView& view : s.sessions) {
        // A session on a listed port is already that port's row: it must not
        // appear twice, once to start and once to stop.
        const int com = view.form.parsed().com;
        const bool listedAbove = std::find(ports.begin(), ports.end(), com) != ports.end();
        if (listedAbove || (!view.session.active()
            && view.session.phase != Phase::Validating))
            continue;
        Row row;
        row.id = "stop:" + std::to_string(view.form.parsed().com);
        row.title = "tio COM" + std::to_string(view.form.parsed().com);
        const char* phase = lang.running;
        if (view.session.phase == Phase::Starting)
            phase = lang.starting;
        else if (view.session.phase == Phase::Stopping)
            phase = lang.stopping;
        else if (view.session.phase == Phase::Validating)
            phase = lang.starting;
        row.subtitle = std::string(phase) + lang.separator
            + serialText(view.form.serial) + lang.separator
            + backendLabel(view.form.backend, lang);
        row.icon = "glyph:U+E71A";
        row.action = "stopSession";
        row.actionKey = "Enter";
        row.score = 95;
        rows.push_back(std::move(row));
    }

    // The configuration, said once: the serial parameters and the backend are
    // global, so one row shows the whole picture and Enter opens the window,
    // where every parameter and the defaults live.
    {
        Row baud;
        baud.id = "baud";
        baud.title = lang.baudrate;
        baud.subtitle = std::to_string(s.serial.baudrate);
        baud.icon = "glyph:U+E8B7";
        baud.action = "openBaudrates";
        baud.actionKey = "Enter";
        baud.copy = baud.subtitle;
        baud.moreActions.emplace_back("openWindow", "Ctrl+Enter");
        baud.score = 60;
        rows.push_back(std::move(baud));
    }

    {
        Row config;
        config.id = "config";
        config.title = lang.openWindow;
        config.subtitle = serialText(s.serial) + lang.separator
            + backendChoiceLabel(s.preference, s.backend, lang);
        config.icon = "glyph:U+E8A9";
        config.action = "openWindow";
        config.actionKey = "Enter";
        config.score = 59;
        rows.push_back(std::move(config));
    }
    return rows;
}

} // namespace tioplug
