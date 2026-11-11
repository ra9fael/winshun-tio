// window_hook.h - the seam between the protocol and the QML window.
//
// The plugin has two faces and one state. The search box needs nothing but the
// protocol, so main.cpp has no Qt in it and builds into both the console test
// binary and TioApp.dll. The window is Qt; it lives in the DLL and reaches the
// same Controller (controller.h).
//
// This is the only thing the protocol needs from the window: a way to ask for
// it. The QML entry installs the hook before serve() starts. In the console
// build nothing installs it, so the action reports that the window needs the Qt
// build rather than silently doing nothing.
#pragma once

#include <functional>

namespace tioplug {

// Called from whichever thread handled the action, so the hook marshals to the
// GUI thread itself. Returns false when the window could not be opened.
using OpenWindowHook = std::function<bool()>;

inline OpenWindowHook& openWindowHook()
{
    static OpenWindowHook hook;
    return hook;
}

inline bool windowAvailable() { return static_cast<bool>(openWindowHook()); }

// Set by the QML entry (qmlapp/), never by the console build.
inline void setOpenWindowHook(OpenWindowHook hook) { openWindowHook() = std::move(hook); }

} // namespace tioplug
