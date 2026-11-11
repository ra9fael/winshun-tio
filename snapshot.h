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

// Everything a view draws, copied out in one go so a view never reads
// half-updated state.
struct Snapshot {
    Port port;
    Serial serial;
    Backend backend = Backend::None;
    Backends backends;
    Session session;
    VerifyResult package;
    std::vector<PortInfo> ports;
    std::wstring identity;
    std::wstring identityTime;
    std::wstring identityName;   // the device's friendly name
    DeviceDetails identityDetails; // what the device says about itself
    bool identityValid = false;
    std::string packageVersion;
    std::string token;
    Readiness readiness;
    std::wstring error;
    std::string diagnostic;
};

} // namespace tioplug
