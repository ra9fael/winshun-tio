// qmlmain.cpp - the entries of TioApp.dll, the standalone window.
//
// winshunTioMain: the plugin itself, as WinShun starts it. Qt goes up on this
// (the GUI) thread; the plugin protocol (plugin.h, the identical handler the
// console tio.exe runs) runs on a worker thread, and the window the search box
// asks for is opened here through the hook.
//
// winshunTioWindow: `tio.exe --window` opens the window directly, no WinShun and
// no protocol: how the window is checked on its own.
//
// The runtime setup mirrors ra9fael.qr and WinShun's winshun.capture: WinShun's
// Qt, WinShun's QML import path, the Basic controls style the shared-Qt contract
// guarantees.
#include "TioWindow.h"
#include "ThemeBridge.h"
#include "qt_window_icon.h"

#include "core.h"
#include "plugin.h"
#include "window_hook.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using namespace Qt::StringLiterals;

namespace {

QQmlApplicationEngine* g_engine = nullptr;
bool g_testDetailsOpen = false;
// --ports-open: the port dropdown starts open, so a screenshot can check the
// entry layout (two lines per entry) without driving the mouse.
bool g_testPortsOpen = false;
// --no-show: the window is built and laid out but never shown, so a run for a
// screenshot or a repro leaves the desktop alone. It still has no native frame,
// which is why --auto drives the QML directly instead of the mouse.
bool g_testHidden = false;
// --auto <action>: the window performs one action as its button would, from a
// timer on the GUI thread. Used to reproduce a click without a visible mouse.
QString g_testAutoAction;

QString appDirectory()
{
    return qEnvironmentVariable("WS_APP_DIR"); // set by the stub
}

// WinShun's theme, read from the desktop: a standalone run has no host to ask,
// so it reads the same settings the host would have reported. The style is not
// knowable here (it is WinShun's own setting), so the window draws fluent's
// neutrals until it runs inside the host.
bool systemDark()
{
    DWORD value = 1;
    DWORD size = sizeof value;
    const LSTATUS status = ::RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return status == ERROR_SUCCESS && value == 0;
}

// Use the Qt application's platform font. Registering a private application
// font here has crashed on some hosts inside QFontDatabase::addApplicationFont;
// QML controls and DetailLine therefore share this same platform font.
void prepareGui()
{
    const QString appDir = appDirectory();
    if (!appDir.isEmpty())
        QCoreApplication::setLibraryPaths({appDir}); // platforms\qwindows.dll: WinShun's
    qputenv("QT_QPA_PLATFORM", "windows:fontengine=gdi");
    QCoreApplication::setOrganizationName(u"WinShun"_s);
    QCoreApplication::setApplicationName(u"WinShun"_s);
    // The shared-Qt contract guarantees QtQuick.Controls.Basic and nothing else;
    // asking for a style the host does not ship would fail to load instead.
    QQuickStyle::setStyle(u"Basic"_s);
}

// The window's notices are Chinese. Writing them with fprintf hands UTF-8
// bytes to a console whose code page is 936, and the line turns into mojibake;
// the console API takes UTF-16 and the console draws it in its own font. A
// redirected stderr is not a console (GetConsoleMode fails) and keeps UTF-8.
void writeStderr(const char* prefix, const QString& text)
{
    const HANDLE handle = ::GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode = 0;
    if (handle != INVALID_HANDLE_VALUE && handle != nullptr
            && ::GetConsoleMode(handle, &mode)) {
        const QString line = QString::fromLatin1(prefix) + text + u'\n';
        DWORD written = 0;
        ::WriteConsoleW(handle, line.utf16(),
            static_cast<DWORD>(line.size()), &written, nullptr);
        return;
    }
    std::fprintf(stderr, "%s%s\n", prefix, qUtf8Printable(text));
}

void startGui()
{
    qunsetenv("QT_QPA_PLATFORM");
    setPluginWindowIcon();
}

// Builds the engine once and loads the window. Returns false with `error` set
// when the QML does not load, so the protocol can say so instead of opening an
// empty frame.
bool openWindow(QString& error)
{
    if (!g_engine) {
        g_engine = new QQmlApplicationEngine;
        const QString appDir = appDirectory();
        if (!appDir.isEmpty())
            g_engine->addImportPath(appDir + u"/qml"_s); // QtQuick and the rest: WinShun's
        tioplug::TioWindow* model = tioplug::windowModel();
        QObject::connect(model, &tioplug::TioWindow::notice, model,
            [](const QString& text) { writeStderr("tio: ", text); });
        g_engine->rootContext()->setContextProperty(u"tio"_s, model);
        // The host's look (nativeLook, the style, the corners, the size): the
        // window draws from it, so it looks like WinShun's own.
        g_engine->rootContext()->setContextProperty(u"themeBridge"_s, tioplug::themeBridge());
    }
    // Standalone runs may ask for the device-details card to start unfolded, so
    // a screenshot or a manual check can read the identity without a click.
    // --details is parsed by the callers; the call lands before the QML loads,
    // so the card's collapsed binding reads the unfolded state directly. The
    // fold key is the localized title, and the run's locale is already set, so
    // both titles are covered.
    if (g_testDetailsOpen) {
        tioplug::windowModel()->setCardCollapsed(u"设备详情"_s, false);
        tioplug::windowModel()->setCardCollapsed(u"Device details"_s, false);
    }
    // A screenshot of the port dropdown: the QML opens the popup on start when
    // this context property is true (absent in a normal run, so nothing opens).
    g_engine->rootContext()->setContextProperty(u"openPortListOnStart"_s, g_testPortsOpen);
    g_engine->rootContext()->setContextProperty(u"testAutoAction"_s, g_testAutoAction);
    if (g_engine->rootObjects().isEmpty()) {
        QObject::connect(g_engine, &QQmlApplicationEngine::warnings, g_engine,
            [](const QList<QQmlError>& warnings) {
                for (const QQmlError& warning : warnings)
                    writeStderr("tio: qml: ", warning.toString());
            });
        g_engine->loadFromModule(u"Tio"_s, u"TioWindow"_s);
        if (g_engine->rootObjects().isEmpty()) {
            error = u"the window's QML did not load"_s;
            return false;
        }
    }
    auto* window = qobject_cast<QQuickWindow*>(g_engine->rootObjects().constFirst());
    if (!window) {
        error = u"the window's root is not a window"_s;
        return false;
    }
    if (g_testHidden) {
        // Laid out at the window's own size, never shown: nothing appears on the
        // desktop, and the QML still builds every delegate and runs the layout.
        window->resize(780, 720);
        return true;
    }
    window->show();
    window->raise();
    window->requestActivate();
    return true;
}

} // namespace

// The plugin: WinShun starts tio.exe, the stub loads this and calls here.
extern "C" __declspec(dllexport) int winshunTioMain(const wchar_t* appDirName)
{
    const QString appDir = QString::fromWCharArray(appDirName);
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    qputenv("WS_APP_DIR", appDir.toUtf8());
    prepareGui();
    int argc = 1;
    char name[] = "TioPlugin";
    char* argv[] = {name, nullptr};
    QGuiApplication app(argc, argv);
    startGui();
    QGuiApplication::setQuitOnLastWindowClosed(false); // the search box keeps working

    // The hook is installed before serve() runs: the first action may already be
    // the one that opens the window.
    tioplug::installWindowHook([] {
        QString error;
        bool opened = false;
        // The action arrives on the protocol thread; QML only on this one.
        QMetaObject::invokeMethod(qGuiApp, [&] { opened = openWindow(error); },
            Qt::BlockingQueuedConnection);
        if (!opened)
            writeStderr("tio: ", error);
        return opened;
    });

    // The plugin protocol on a worker thread: serve() blocks on stdin, the GUI
    // event loop runs here. serve() returning (shutdown, stdin closed) is the
    // process's end.
    int rc = 0;
    std::thread serve([&] {
        rc = tioplug::runServeLoop();
        QMetaObject::invokeMethod(&app, &QCoreApplication::quit, Qt::QueuedConnection);
    });
    app.exec();
    serve.join();
    return rc;
}

// The standalone check: the window and nothing else. --en and --dark override
// what the host would have told the plugin.
extern "C" __declspec(dllexport) int winshunTioWindow(const wchar_t* appDirName)
{
    const QString appDir = QString::fromWCharArray(appDirName);
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    qputenv("WS_APP_DIR", appDir.toUtf8());

    bool zh = true;
    bool dark = false;
    std::wstring dataDir;
    int count = 0;
    if (LPWSTR* wide = ::CommandLineToArgvW(::GetCommandLineW(), &count)) {
        for (int i = 1; i < count; ++i) {
            const std::wstring arg(wide[i]);
            if (arg == L"--en")
                zh = false;
            if (arg == L"--dark")
                dark = true;
            if (arg == L"--details")
                g_testDetailsOpen = true;
            if (arg == L"--ports-open")
                g_testPortsOpen = true;
            if (arg == L"--no-show")
                g_testHidden = true;
            if (arg == L"--auto" && i + 1 < count)
                g_testAutoAction = QString::fromWCharArray(wide[i + 1]);
            if (arg == L"--data-dir" && i + 1 < count)
                dataDir = wide[i + 1];
        }
        ::LocalFree(wide);
    }
    if (dataDir.empty()) {
        wchar_t temp[MAX_PATH * 2];
        const DWORD length = ::GetTempPathW(static_cast<DWORD>(sizeof temp / sizeof temp[0]), temp);
        dataDir = length ? std::wstring(temp, length) + L"tio-preview" : L".";
        ::CreateDirectoryW(dataDir.c_str(), nullptr);
    }

    tioplug::core().setLocale(zh ? "zh" : "en");
    tioplug::core().setTheme(dark ? "dark" : "light");
    // A standalone run has no host to ask for nativeLook: the window draws the
    // fallback table (fluent's neutrals) at the desktop's dark setting. The
    // test hook TIO_TEST_APPEARANCE carries a whole appearance object, so the
    // fallback/native switch and the four styles are exercised without WinShun.
    if (const char* hook = std::getenv("TIO_TEST_APPEARANCE")) {
        std::string problem;
        if (const auto parsed = winshun::Json::parse(hook, &problem))
            tioplug::core().setAppearance(*parsed);
        else
            std::fprintf(stderr, "tio: TIO_TEST_APPEARANCE is not JSON: %s\n", problem.c_str());
    } else {
        tioplug::core().setAppearance(winshun::Json::Object{});
    }
    // The window reads the real plugin folder, so the package check and the
    // backend probe report what they would in WinShun.
    const std::wstring self = [] {
        std::wstring path(32768, L'\0');
        const DWORD length = ::GetModuleFileNameW(nullptr, path.data(),
            static_cast<DWORD>(path.size()));
        path.resize(length);
        const std::size_t slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring(L".") : path.substr(0, slash);
    }();
    tioplug::core().configure(self, winshun::utf8(dataDir));

    prepareGui();
    int argc = 1;
    char name[] = "TioWindow";
    char* argv[] = {name, nullptr};
    QGuiApplication app(argc, argv);
    startGui();
    QGuiApplication::setQuitOnLastWindowClosed(true); // the window ending ends the run

    QString error;
    if (!openWindow(error)) {
        writeStderr("tio: ", error);
        std::fflush(stderr);
        return 2;
    }
    return app.exec();
}
