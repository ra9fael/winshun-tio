// ShareClient.cpp - Winsock implementation of the window's read-only consumer.
#include "ShareClient.h"

#include <ws2tcpip.h>

#include <windows.h>

#include <algorithm>
#include <chrono>

namespace tioplug {

ShareClient::~ShareClient()
{
    stop();
}

void ShareClient::append(const char* data, std::size_t size)
{
    if (size == 0)
        return;
    {
        const std::lock_guard lock(m_mutex);
        m_buffer.append(data, size);
        // Keep only the tail the views can show. The socket is still drained in
        // full so the producer never blocks on this client.
        if (m_buffer.size() > kMaxBufferedBytes) {
            const std::size_t drop = m_buffer.size() - kMaxBufferedBytes;
            m_buffer.erase(0, drop);
            m_bufferStart += drop;
        }
        m_total.fetch_add(size);
        m_screen.feed(data, size);
    }
    m_revision.fetch_add(1);
}

std::string ShareClient::consume(std::uint64_t& cursor) const
{
    const std::lock_guard lock(m_mutex);
    const std::uint64_t end = m_bufferStart + m_buffer.size();
    if (cursor < m_bufferStart)
        cursor = m_bufferStart;   // the oldest byte still held
    if (cursor >= end)
        return std::string();
    const std::size_t offset = static_cast<std::size_t>(cursor - m_bufferStart);
    std::string out = m_buffer.substr(offset);
    cursor = end;
    return out;
}

std::string ShareClient::tail(std::size_t maxBytes) const
{
    const std::lock_guard lock(m_mutex);
    if (maxBytes == 0 || m_buffer.size() <= maxBytes)
        return m_buffer;
    return m_buffer.substr(m_buffer.size() - maxBytes);
}

std::vector<AnsiLine> ShareClient::lines() const
{
    const std::lock_guard lock(m_mutex);
    return m_screen.lines();
}

bool ShareClient::write(const std::string& data)
{
    if (m_readOnly.load() || data.empty())
        return false;
    const SOCKET socket = m_socket.load();
    if (socket == INVALID_SOCKET)
        return false;
    const std::lock_guard lock(m_writeMutex);
    const int sent = ::send(socket, data.data(), static_cast<int>(data.size()), 0);
    return sent == static_cast<int>(data.size());
}

void ShareClient::clear()
{
    {
        const std::lock_guard lock(m_mutex);
        m_buffer.clear();
        m_bufferStart = m_total.load();
        m_screen.clear();
    }
    m_revision.fetch_add(1);
}

bool ShareClient::start(const std::string& host, std::uint16_t port)
{
    stop();
    if (port == 0)
        return false;

    WSADATA data{};
    if (::WSAStartup(MAKEWORD(2, 2), &data) != 0)
        return false;
    m_wsaReady = true;

    m_stop.store(false);
    m_connected.store(false);
    m_port.store(port);
    m_stop.store(false);
    // A new connection starts a new stream: keeping the old offsets would put a
    // consumer's cursor past bytes it has never seen.
    {
        const std::lock_guard lock(m_mutex);
        m_buffer.clear();
        m_screen.clear();
        m_bufferStart = 0;
    }
    m_total.store(0);
    m_revision.fetch_add(1);
    m_running.store(true);
    try {
        m_thread = std::thread(&ShareClient::run, this, host, port);
    } catch (...) {
        m_running.store(false);
        m_wsaReady = false;
        ::WSACleanup();
        return false;
    }
    return true;
}

void ShareClient::stop()
{
    m_stop.store(true);
    if (m_thread.joinable())
        m_thread.join();
    m_running.store(false);
    m_connected.store(false);
    m_port.store(0);
    if (m_wsaReady) {
        ::WSACleanup();
        m_wsaReady = false;
    }
}

void ShareClient::run(std::string host, std::uint16_t port)
{
    char receive[16 * 1024];
    while (!m_stop.load()) {
        SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == INVALID_SOCKET) {
            ::Sleep(250);
            continue;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1)
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        bool connected = false;
        for (int attempt = 0; attempt < 40 && !m_stop.load(); ++attempt) {
            // Non-blocking connect: a stopped client must not wait out the whole
            // retry window, and the GUI thread calls stop() on session teardown.
            u_long enabled = 1;
            ::ioctlsocket(socket, FIONBIO, &enabled);
            const int result = ::connect(socket, reinterpret_cast<const sockaddr*>(&address),
                sizeof address);
            if (result == 0) {
                connected = true;
                break;
            }
            if (::WSAGetLastError() != WSAEWOULDBLOCK)
                break;
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(socket, &writable);
            fd_set failed;
            FD_ZERO(&failed);
            FD_SET(socket, &failed);
            timeval wait{0, 100 * 1000};
            const int ready = ::select(static_cast<int>(socket + 1), nullptr, &writable,
                &failed, &wait);
            if (ready > 0 && FD_ISSET(socket, &failed))
                break;
            if (ready > 0 && FD_ISSET(socket, &writable)) {
                connected = true;
                break;
            }
        }
        if (connected) {
            u_long blocking = 0;
            ::ioctlsocket(socket, FIONBIO, &blocking);
        }
        if (!connected) {
            ::closesocket(socket);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        m_connected.store(true);
        m_socket.store(socket);
        while (!m_stop.load()) {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(socket, &readable);
            timeval timeout{0, 250 * 1000};
            const int selected = ::select(static_cast<int>(socket + 1), &readable,
                nullptr, nullptr, &timeout);
            if (selected == SOCKET_ERROR)
                break;
            if (selected == 0)
                continue;
            const int count = ::recv(socket, receive, sizeof receive, 0);
            if (count <= 0)
                break;
            // The buffer keeps only the tail the window shows, but the socket is
            // drained in full so a slow renderer can never stall tio.
            append(receive, static_cast<std::size_t>(count));
        }

        m_socket.store(INVALID_SOCKET);
        m_connected.store(false);
        ::shutdown(socket, SD_BOTH);
        ::closesocket(socket);
        if (!m_stop.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
    m_connected.store(false);
}

} // namespace tioplug
