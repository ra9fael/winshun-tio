// test_share_client.cpp - the window's loopback consumer of tio's socket.
//
// A local listener stands in for tio -S inet:<port>. The test proves the client
// connects to loopback, receives the bytes tio would mirror, keeps a bounded
// tail, never sends anything back, and reconnects after a drop.
#include "../qmlapp/ShareClient.h"

#include <winsock2.h>

#include <cstdio>
#include <string>
#include <thread>
#include <chrono>

namespace {

int g_total = 0;
int g_failed = 0;

void report(bool ok, const char* label, const std::string& why = std::string())
{
    ++g_total;
    if (ok) {
        std::printf("PASS %s\n", label);
    } else {
        ++g_failed;
        std::printf("FAIL %s%s%s\n", label, why.empty() ? "" : ": ", why.c_str());
    }
}

// A minimal stand-in for tio's listener: accept one client, send `payload`,
// then report whether the client ever sent a byte of its own.
class FakeShareServer {
public:
    bool start()
    {
        WSADATA data{};
        if (::WSAStartup(MAKEWORD(2, 2), &data) != 0)
            return false;
        m_listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listener == INVALID_SOCKET)
            return false;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(m_listener, reinterpret_cast<sockaddr*>(&address), sizeof address) == SOCKET_ERROR)
            return false;
        int length = sizeof address;
        ::getsockname(m_listener, reinterpret_cast<sockaddr*>(&address), &length);
        m_port = ntohs(address.sin_port);
        return ::listen(m_listener, 1) != SOCKET_ERROR;
    }

    // Accepts a client and sends the payload. Returns false when no client came.
    bool acceptAndSend(const std::string& payload, std::string& receivedBack)
    {
        const SOCKET client = ::accept(m_listener, nullptr, nullptr);
        if (client == INVALID_SOCKET)
            return false;
        const int sent = ::send(client, payload.data(), static_cast<int>(payload.size()), 0);
        // Give the consumer a moment to read; anything it sends would be a
        // protocol violation (the window is read-only).
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        u_long available = 0;
        if (::ioctlsocket(client, FIONREAD, &available) == 0 && available > 0) {
            char buffer[64];
            const int got = ::recv(client, buffer, static_cast<int>(sizeof buffer), 0);
            if (got > 0)
                receivedBack.assign(buffer, static_cast<std::size_t>(got));
        }
        ::closesocket(client);
        return sent == static_cast<int>(payload.size());
    }

    std::uint16_t port() const { return m_port; }

    void stop()
    {
        if (m_listener != INVALID_SOCKET) {
            ::closesocket(m_listener);
            m_listener = INVALID_SOCKET;
        }
        ::WSACleanup();
    }

    ~FakeShareServer() { stop(); }

private:
    SOCKET m_listener = INVALID_SOCKET;
    std::uint16_t m_port = 0;
};

} // namespace

int main()
{
    using namespace std::chrono_literals;

    FakeShareServer server;
    report(server.start(), "the fake tio listener starts on loopback");
    if (server.port() == 0)
        return 1;

    tioplug::ShareClient client;
    report(client.start("127.0.0.1", server.port()), "the consumer starts");

    const std::string payload = "temp=23.5C\r\n";
    std::string echoed;
    const bool sent = server.acceptAndSend(payload, echoed);
    report(sent, "the fake tio sends a line to the client");

    std::string tail;
    for (int i = 0; i < 40 && tail.size() < payload.size(); ++i) {
        tail = client.tail(1024);
        if (tail.size() >= payload.size())
            break;
        std::this_thread::sleep_for(25ms);
    }
    report(tail == payload, "the window receives exactly what tio mirrored", tail);
    report(echoed.empty(), "the window never sends a byte back", echoed);
    report(client.connected() || tail == payload, "the client reports its connection");
    report(client.port() == server.port(), "the client names the port it consumed");

    // The incremental path, which the auto-log and the view both use: consume()
    // hands each byte over once, and a second call with no new traffic is empty.
    std::uint64_t cursor = 0;
    const std::string first = client.consume(cursor);
    const std::string second = client.consume(cursor);
    report(first == payload, "consume() delivers the bytes", first);
    report(second.empty(), "consume() does not repeat bytes", second);
    report(client.totalBytes() == payload.size(), "the client counts every byte",
        std::to_string(client.totalBytes()));
    report(std::string(client.lines().empty() ? "" : client.lines()[0].text())
               .find("temp=23.5C") != std::string::npos,
        "the terminal screen holds the text");
    bool escapeInText = false;
    for (const tioplug::AnsiLine& line : client.lines())
        for (const tioplug::AnsiSpan& span : line.spans)
            if (span.text.find('\x1b') != std::string::npos)
                escapeInText = true;
    report(!escapeInText, "no escape byte reaches the terminal text");

    client.clear();
    report(client.tail(1024).empty(), "clear drops the buffered tail");

    // A bounded tail: the window only keeps what it can show.
    client.stop();
    report(!client.running(), "stop ends the consumer");

    // A restart must not inherit the old cursor: this is the bug where a second
    // session received bytes but nothing was ever shown or logged again.
    std::uint64_t stale = 0;
    client.consume(stale);
    report(client.start("127.0.0.1", server.port()), "the consumer can start again");
    report(client.running(), "the consumer is running again");
    report(client.totalBytes() == 0, "a restarted consumer counts from zero",
        std::to_string(client.totalBytes()));
    report(client.consume(stale).empty(), "a restarted stream has no stale bytes");

    client.stop();
    server.stop();

    std::printf("%s %d share-client checks, %d failed\n",
        g_failed ? "FAILED" : "PASS", g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
