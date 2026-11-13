// ShareClient.h - the window's consumer of tio's shared socket.
//
// tio -S inet:<port> mirrors the serial session over a TCP socket. The window
// connects to 127.0.0.1 and reads by default: nothing is sent unless the user
// explicitly turns writing on, and even then only what the terminal view sends
// (nothing at all today), so the GUI never takes the port away from tio.
//
// The thread reconnects while tio is still binding the listener, drains the
// socket continuously (a slow reader would stall tio), reduces ANSI escape
// sequences into terminal lines for the view, and keeps a bounded tail of the
// raw bytes for the plain and hex views.
#pragma once

#include "AnsiText.h"

#include <winsock2.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace tioplug {

class ShareClient {
public:
    ShareClient() = default;
    ~ShareClient();

    ShareClient(const ShareClient&) = delete;
    ShareClient& operator=(const ShareClient&) = delete;

    bool start(const std::string& host, std::uint16_t port);
    void stop();

    bool running() const { return m_running.load(); }
    bool connected() const { return m_connected.load(); }
    std::uint16_t port() const { return m_port.load(); }
    bool readOnly() const { return m_readOnly.load(); }

    // Whether received bytes are only read (default) or may be written back to
    // the device. Turning writing off does not drop the connection.
    void setReadOnly(bool readOnly) { m_readOnly.store(readOnly); }

    // Sends bytes to tio. Refused while read-only, so a display bug cannot write
    // the serial device by accident.
    bool write(const std::string& data);

    // The most recent `maxBytes` of received raw bytes.
    std::string tail(std::size_t maxBytes) const;
    // Bytes received after `cursor` (an absolute byte offset from the start of
    // the connection), and moves `cursor` to the end of what it returns. The
    // buffer is bounded, so a cursor older than it is clamped to the oldest byte
    // still held. This is what the log writer and the views consume, so nothing
    // is written twice.
    std::string consume(std::uint64_t& cursor) const;
    std::uint64_t totalBytes() const { return m_total.load(); }
    // The terminal screen reduced from those bytes, copied under the lock so a
    // repaint never reads a line the thread is still writing.
    std::vector<AnsiLine> lines() const;
    // Monotonic counter bumped whenever the screen changes.
    std::uint64_t revision() const { return m_revision.load(); }
    void clear();

    static constexpr std::size_t kMaxBufferedBytes = 256 * 1024;

private:
    void run(std::string host, std::uint16_t port);
    void append(const char* data, std::size_t size);

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_readOnly{true};
    std::atomic<std::uint16_t> m_port{0};
    std::atomic<std::uint64_t> m_revision{0};
    // Absolute offset of m_buffer.front() from the first byte of the session.
    std::uint64_t m_bufferStart = 0;
    std::atomic<std::uint64_t> m_total{0};
    mutable std::mutex m_mutex;
    std::mutex m_writeMutex;
    std::string m_buffer;
    AnsiText m_screen;
    // The live connection, only touched by the thread and write().
    std::atomic<SOCKET> m_socket{INVALID_SOCKET};
    bool m_wsaReady = false;
};

} // namespace tioplug
