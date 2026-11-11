// snapshot.h - what a view draws, copied out in one go.
//
// Split from controller.h so the row builder (rows_build.h) and its tests can
// read it without pulling the Win32 layer in: Snapshot is plain data, and the
// test for the rows runs on any machine.
#pragma once

#include "logic.h"
#include "native.h"

#include <string>
#include <vector>

namespace tioplug {

// One session tab, copied out for the views: the session state machine and the
// form the tab holds. A tab owns at most one port; two tabs never share one.
struct SessionView {
    Session session;
    Form form;
    bool focused = false; // the tab the window is drawing
};

// Everything a view draws, copied out in one go so a view never reads
// half-updated state.
struct Snapshot {
    // The default configuration: what the search box edits and what the next
    // session starts from. It is not a tab; a tab binds a port to a process.
    Port port;
    Serial serial;
    Backend backend = Backend::None;

    // The session tabs. Empty only before the first tab exists.
    std::vector<SessionView> sessions;

    Backends backends;
    VerifyResult package;
    std::vector<PortInfo> ports;
    std::wstring identity;         // the focused tab's port identity
    std::wstring identityTime;
    std::wstring identityName;     // the device's friendly name
    DeviceDetails identityDetails; // what the device says about itself
    bool identityValid = false;
    std::string packageVersion;
    Readiness readiness;         // the focused tab's gates (the Launch button)
    Readiness defaultReadiness;  // the default form's gates (the search box row)
    std::wstring error;
    std::string diagnostic;
};

} // namespace tioplug
