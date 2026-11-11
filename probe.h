// probe.h - the read-only diagnostic entry point.
//
// `tio.exe --probe ports | backends | verify <folder>` is what the native
// contract tests and tools/package.py call. It never opens a port and never
// starts a process, and it needs no Qt, so the starter answers it before loading
// TioApp.dll: a check must not fail because the machine's Qt is another version.
#pragma once

#include "native.h"

#include <string>

namespace tioplug {

inline int runProbe(int argc, char** argv)
{
    const std::string what = argc >= 3 ? std::string(argv[2]) : std::string("all");
    if (what == "ports") {
        for (const PortInfo& info : enumeratePorts()) {
            winshun::detail::writeLine("COM" + std::to_string(info.com) + "\t"
                + winshun::utf8(info.instanceId));
            // What the device says about itself, so the collection can be
            // checked on a machine without the window.
            const DeviceDetails& details = info.details;
            winshun::detail::writeLine(std::string("  name=") + winshun::utf8(info.description)
                + " bus=" + winshun::utf8(details.busType)
                + " vid=" + winshun::utf8(details.vendorId)
                + " pid=" + winshun::utf8(details.productId)
                + " serial=" + winshun::utf8(details.serialNumber)
                + " mfr=" + winshun::utf8(details.manufacturer)
                + " chip=" + winshun::utf8(details.chip)
                + " driver=" + winshun::utf8(details.driverVersion)
                + " location=" + winshun::utf8(details.location)
                + " problem=" + winshun::utf8(details.problem));
        }
        return 0;
    }
    if (what == "backends") {
        const Backends found = probeBackends();
        winshun::detail::writeLine(std::string("wt=") + (found.wt ? "1" : "0")
            + " version=" + found.wtVersion + " conhost=" + (found.conhost ? "1" : "0"));
        return 0;
    }
    if (what == "verify" && argc >= 4) {
        const VerifyResult result = verifyPackage(winshun::wide(argv[3]));
        winshun::detail::writeLine(std::string(result.ok ? "ok" : "failed") + " checked="
            + std::to_string(result.checked) + " problems="
            + std::to_string(result.problems.size()));
        for (const std::string& problem : result.problems)
            winshun::detail::writeLine("  " + problem);
        return result.ok ? 0 : 1;
    }
    return 2;
}

} // namespace tioplug
