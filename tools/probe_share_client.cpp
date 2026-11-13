// probe_share_client.cpp - proves the window's ShareClient reads a real tio socket.
//
// Arguments: <port> <marker>. It connects as the window does (read-only) and
// prints what it received, so the GUI's data path is checked against the real
// tio.exe rather than the fake listener used by the unit test.
#include "../qmlapp/ShareClient.h"

#include <cstdio>
#include <string>
#include <thread>
#include <chrono>

int main(int argc, char** argv)
{
    const int port = argc >= 2 ? std::atoi(argv[1]) : 54321;
    const std::string marker = argc >= 3 ? argv[2] : "WINSHUN-ECHO-CHECK\r\n";

    tioplug::ShareClient client;
    if (!client.start("127.0.0.1", static_cast<std::uint16_t>(port))) {
        std::printf("FAIL could not start the client\n");
        return 1;
    }

    // Wait for the connection and the echo.
    std::string seen;
    for (int i = 0; i < 60; ++i) {
        seen = client.tail(64 * 1024);
        if (seen.find(marker) != std::string::npos)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    // The incremental path the window actually uses: consume() must hand every
    // byte over exactly once, and a second call with no new traffic must be
    // empty (otherwise the auto-log would repeat itself every poll).
    std::uint64_t cursor = 0;
    const std::string all = client.consume(cursor);
    const std::string again = client.consume(cursor);

    // The terminal screen: the same bytes reduced, with the marker in the text
    // and no escape byte left in it.
    std::string screen;
    for (const tioplug::AnsiLine& line : client.lines())
        screen += line.text();
    bool escapeInText = false;
    for (const tioplug::AnsiLine& line : client.lines())
        for (const tioplug::AnsiSpan& span : line.spans)
            if (span.text.find('\x1b') != std::string::npos)
                escapeInText = true;

    const bool connected = client.connected();
    const bool found = seen.find(marker) != std::string::npos;
    std::printf("connected=%d received=%zu\n", connected ? 1 : 0, seen.size());
    if (!seen.empty())
        std::printf("sample=%s\n", seen.substr(0, 160).c_str());
    std::printf("consume=%zu second=%zu total=%llu\n", all.size(), again.size(),
        static_cast<unsigned long long>(client.totalBytes()));
    client.stop();
    std::printf("%s the GUI consumer read the shared serial bytes\n",
        found ? "PASS" : "INFO");
    std::printf("%s consume() delivered the bytes once\n",
        !all.empty() && again.empty() ? "PASS" : "FAIL");
    std::printf("%s the terminal screen kept the text\n",
        screen.find(marker.substr(0, marker.size() - 2)) != std::string::npos ? "PASS" : "FAIL");
    std::printf("%s the terminal screen holds no escape byte\n",
        escapeInText ? "FAIL" : "PASS");
    return found && !all.empty() && again.empty() ? 0 : 1;
}
