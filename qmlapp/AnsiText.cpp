// AnsiText.cpp - the ANSI/VT reducer behind the shared-content view.
#include "AnsiText.h"

#include <algorithm>
#include <cstdio>

namespace tioplug {

namespace {

// The 16 ANSI colours live in kAnsiPalette (exported, because a view resolves
// them against its own light/dark theme). Only the cube levels are private.

// The six levels of the xterm 6x6x6 cube. Not a linear ramp: this is the table
// every terminal uses, and a shell prompt tuned against it looks right here.
const int kCube[6] = {0, 95, 135, 175, 215, 255};

// Reads the parameter list of a CSI sequence: digits and ';', empty means 0.
std::vector<int> readParams(const std::string& text)
{
    std::vector<int> values;
    int current = 0;
    bool any = false;
    for (const char c : text) {
        if (c >= '0' && c <= '9') {
            current = current * 10 + (c - '0');
            any = true;
        } else if (c == ';') {
            values.push_back(any ? current : 0);
            current = 0;
            any = false;
        } else if (c == '?') {
            // Private mode (cursor visibility): the parameters after it are not
            // SGR values; the caller drops the whole sequence anyway.
            any = false;
        }
    }
    values.push_back(any ? current : 0);
    return values;
}

} // namespace

// The palette is published from the same table the reducer uses, so a view and
// the 256-colour table can never disagree about what colour 4 is.
const int kAnsiPalette[16] = {
    0x1b1b1b, 0xc42b1c, 0x0f7b0f, 0x9d5d00,
    0x0f6cbd, 0x8a2be2, 0x0f7b8a, 0x6b6b6b,
    0x8a8a8a, 0xff6b6b, 0x6ccb5f, 0xe0a33c,
    0x4cc2ff, 0xd48cff, 0x4fc3d4, 0xd5d5d5,
};

int ansi256(int index)
{
    if (index < 0)
        return kAnsiPalette[0];
    if (index < 16)
        return kAnsiPalette[index];
    if (index < 232) {
        const int cube = index - 16;
        const int r = kCube[(cube / 36) % 6];
        const int g = kCube[(cube / 6) % 6];
        const int b = kCube[cube % 6];
        return (r << 16) | (g << 8) | b;
    }
    // 232..255: the grey ramp, 8, 18, 28, … 238.
    const int grey = 8 + (index - 232) * 10;
    return (grey << 16) | (grey << 8) | grey;
}

bool AnsiLine::hasText() const
{
    for (const AnsiSpan& span : spans)
        if (!span.text.empty())
            return true;
    return false;
}

std::string AnsiLine::text() const
{
    std::string out;
    for (const AnsiSpan& span : spans)
        out += span.text;
    return out;
}

void AnsiText::clear()
{
    m_lines.assign(1, AnsiLine{});
    m_row = 0;
    m_col = 0;
    m_color = -1;
    m_colorIndex = -1;
    m_background = -1;
    m_backgroundIndex = -1;
    m_bold = false;
    m_inverse = false;
    m_pending.clear();
    m_dirty = false;
}

void AnsiText::sgr(const std::vector<int>& params)
{
    for (std::size_t i = 0; i < params.size(); ++i) {
        const int p = params[i];
        if (p == 0) {
            m_color = -1;
            m_colorIndex = -1;
            m_background = -1;
            m_backgroundIndex = -1;
            m_bold = false;
            m_inverse = false;
        } else if (p == 1) {
            m_bold = true;
        } else if (p == 22) {
            m_bold = false;
        } else if (p == 7) {
            m_inverse = true;
        } else if (p == 27) {
            m_inverse = false;
        } else if (p >= 30 && p <= 37) {
            m_color = kAnsiPalette[p - 30];
            m_colorIndex = p - 30;
        } else if (p >= 90 && p <= 97) {
            m_color = kAnsiPalette[p - 90 + 8];
            m_colorIndex = p - 90 + 8;
        } else if (p == 39) {
            m_color = -1;
            m_colorIndex = -1;
        } else if (p >= 40 && p <= 47) {
            m_background = kAnsiPalette[p - 40];
            m_backgroundIndex = p - 40;
        } else if (p >= 100 && p <= 107) {
            m_background = kAnsiPalette[p - 100 + 8];
            m_backgroundIndex = p - 100 + 8;
        } else if (p == 49) {
            m_background = -1;
            m_backgroundIndex = -1;
        } else if (p == 38 || p == 48) {
            // 38;5;n / 48;5;n (256-colour) and 38;2;r;g;b / 48;2;r;g;b
            // (truecolour). The extended values are consumed either way, so a
            // background never leaves its numbers behind as text.
            int skip = 0;
            if (i + 1 < params.size() && params[i + 1] == 5)
                skip = 2;
            else if (i + 1 < params.size() && params[i + 1] == 2)
                skip = 4;
            int value = -1;
            int index = -1;
            if (skip == 4 && i + 4 < params.size())
                value = (params[i + 2] << 16) | (params[i + 3] << 8) | params[i + 4];
            else if (skip == 2 && i + 2 < params.size()) {
                // The full xterm table, not a 16-colour modulo: 208 is orange,
                // and 208 % 16 == 0 turned it near-black.
                index = params[i + 2];
                value = ansi256(index);
                if (index >= 16)
                    index = -1;   // a fixed RGB, not a palette entry
            }
            if (p == 38) {
                m_color = value;
                m_colorIndex = index;
            } else {
                m_background = value;
                m_backgroundIndex = index;
            }
            i += static_cast<std::size_t>(skip);
        }
    }
}

void AnsiText::eraseLine(int mode)
{
    AnsiLine& line = m_lines[m_row];
    if (mode == 2 || mode == 0) {
        // Erase to end (0) keeps the text before the cursor; erase whole line
        // (2) drops it. Both drop what follows the cursor.
        std::size_t kept = 0;
        std::size_t columns = 0;
        std::vector<AnsiSpan> spans;
        for (const AnsiSpan& span : line.spans) {
            if (columns >= m_col)
                break;
            const std::size_t room = m_col - columns;
            if (span.text.size() <= room) {
                spans.push_back(span);
                columns += span.text.size();
            } else {
                AnsiSpan part = span;
                part.text = span.text.substr(0, room);
                spans.push_back(part);
                columns += room;
            }
            kept += 1;
        }
        if (mode == 2 || kept == 0)
            spans.clear();
        line.spans = std::move(spans);
    }
}

void AnsiText::put(char32_t ch)
{
    if (m_col >= kMaxColumns)
        return;
    AnsiLine& line = m_lines[m_row];

    AnsiSpan fresh;
    fresh.color = m_color;
    fresh.colorIndex = m_colorIndex;
    fresh.background = m_background;
    fresh.backgroundIndex = m_backgroundIndex;
    fresh.bold = m_bold;
    fresh.inverse = m_inverse;
    fresh.text.assign(1, static_cast<char>(ch & 0xFF));

    // A carriage return moves the cursor back over text that is still there: the
    // next character *replaces* the old one, which is how progress bars and a
    // re-drawn login prompt behave. Appending instead is what turned
    // "progress 10%\rprogress 99%" into one long line.
    std::size_t length = 0;
    for (const AnsiSpan& span : line.spans)
        length += span.text.size();

    if (m_col < length) {
        std::size_t pos = 0;
        for (std::size_t i = 0; i < line.spans.size(); ++i) {
            AnsiSpan& span = line.spans[i];
            const std::size_t spanLength = span.text.size();
            if (m_col < pos + spanLength) {
                const std::size_t offset = m_col - pos;
                // What followed the overwritten character keeps its own style.
                const std::string tail = span.text.substr(offset + 1);
                span.text.erase(offset);
                const auto at = line.spans.begin() + static_cast<std::ptrdiff_t>(i + 1);
                if (tail.empty()) {
                    line.spans.insert(at, fresh);
                } else {
                    AnsiSpan rest = span;
                    rest.text = tail;
                    const auto inserted = line.spans.insert(at, rest);
                    line.spans.insert(inserted, fresh);
                }
                break;
            }
            pos += spanLength;
        }
        // Overwrites split spans; put the runs back together so a line drawn by
        // hundreds of CR updates is not hundreds of spans.
        std::vector<AnsiSpan> merged;
        for (AnsiSpan& span : line.spans) {
            if (!merged.empty() && merged.back().color == span.color
                && merged.back().colorIndex == span.colorIndex
                && merged.back().background == span.background
                && merged.back().backgroundIndex == span.backgroundIndex
                && merged.back().bold == span.bold
                && merged.back().inverse == span.inverse) {
                merged.back().text += span.text;
            } else if (!span.text.empty()) {
                merged.push_back(span);
            }
        }
        line.spans = std::move(merged);
    } else if (!line.spans.empty() && line.spans.back().color == m_color
        && line.spans.back().colorIndex == m_colorIndex
        && line.spans.back().background == m_background
        && line.spans.back().backgroundIndex == m_backgroundIndex
        && line.spans.back().bold == m_bold && line.spans.back().inverse == m_inverse) {
        // Extend the last span when the style matches: one span per run, not per
        // character.
        line.spans.back().text.push_back(static_cast<char>(ch & 0xFF));
    } else {
        line.spans.push_back(std::move(fresh));
    }
    ++m_col;
    m_dirty = true;
}

void AnsiText::carriageReturn()
{
    m_col = 0;
    m_dirty = true;
}

void AnsiText::backspace()
{
    if (m_col > 0)
        --m_col;
    m_dirty = true;
}

void AnsiText::newline()
{
    if (m_row + 1 >= m_lines.size())
        m_lines.emplace_back();
    ++m_row;
    m_col = 0;
    if (m_lines.size() > kMaxLines) {
        const std::size_t drop = m_lines.size() - kMaxLines;
        m_lines.erase(m_lines.begin(), m_lines.begin() + static_cast<std::ptrdiff_t>(drop));
        m_row = m_lines.size() - 1;
    }
    m_dirty = true;
}

bool AnsiText::feed(const char* data, std::size_t size)
{
    if (!data || size == 0)
        return false;
    m_dirty = false;

    // Finish a sequence split across calls first: prepend what was kept.
    if (!m_pending.empty()) {
        std::string combined = m_pending;
        combined.append(data, size);
        m_pending.clear();
        feedText(combined.data(), combined.size());
        return m_dirty;
    }
    feedText(data, size);
    return m_dirty;
}

// The actual scanner. Kept separate so a pending sequence can be re-fed with
// the next read's bytes without duplicating the state machine.
bool AnsiText::feedText(const char* data, std::size_t size)
{
    std::size_t i = 0;
    while (i < size) {
        const unsigned char c = static_cast<unsigned char>(data[i]);
        if (c != 0x1b) {
            if (c == '\r') {
                carriageReturn();
            } else if (c == '\n') {
                newline();
            } else if (c == '\b') {
                backspace();
            } else if (c == '\t') {
                const std::size_t to = (m_col + 8) & ~static_cast<std::size_t>(7);
                while (m_col < to && m_col < kMaxColumns)
                    put(' ');
            } else if (c == 0x07) {
                // bell
            } else if (c >= 0x20 || c >= 0x80) {
                put(c);
            }
            ++i;
            continue;
        }

        // ESC: collect the sequence. CSI is ESC '[' ... final-byte 0x40..0x7E;
        // everything else is a two-byte escape and can be dropped.
        if (i + 1 >= size) {
            m_pending.assign(data + i, size - i);
            break;
        }
        const unsigned char next = static_cast<unsigned char>(data[i + 1]);
        if (next == '[') {
            std::size_t j = i + 2;
            std::string params;
            while (j < size) {
                const unsigned char p = static_cast<unsigned char>(data[j]);
                if (p >= 0x40 && p <= 0x7E)
                    break;
                params.push_back(static_cast<char>(p));
                ++j;
            }
            if (j >= size) {
                m_pending.assign(data + i, size - i);
                break;
            }
            const char finalByte = data[j];
            if (finalByte == 'm')
                sgr(readParams(params));
            else if (finalByte == 'K')
                eraseLine(params.empty() ? 0 : readParams(params).front());
            // Cursor moves, erases, mode set/reset and the rest are dropped.
            m_dirty = true;
            i = j + 1;
            continue;
        }
        if (next == ']') {
            // OSC: ends with BEL or ESC '\'. `closed` distinguishes "ended at the
            // last byte" from "the read was cut short", which a bare j >= size
            // cannot.
            std::size_t j = i + 2;
            bool closed = false;
            while (j < size) {
                if (static_cast<unsigned char>(data[j]) == 0x07) {
                    ++j;
                    closed = true;
                    break;
                }
                if (static_cast<unsigned char>(data[j]) == 0x1b && j + 1 < size
                    && data[j + 1] == '\\') {
                    j += 2;
                    closed = true;
                    break;
                }
                ++j;
            }
            if (!closed) {
                m_pending.assign(data + i, size - i);
                break;
            }
            m_dirty = true;
            i = j;
            continue;
        }
        // A two-byte escape (ESC c reset, ESC 7/8 save/restore, …): drop it.
        m_dirty = true;
        i += 2;
    }
    return m_dirty;
}

// A plain 16-bytes-per-line dump, grouped 8 + 8.
//
// There is deliberately no offset column: the dump is built from a sliding
// window of the last received bytes, so an offset would be the position inside
// that window, not in the session, and it changed every time the window slid.
// It looked like data and always read as a row of leading zeros.
std::string asHexDump(const std::string& bytes, std::size_t maxBytes, int perRow, bool ascii)
{
    if (bytes.empty())
        return std::string();
    const std::size_t row = perRow <= 0 ? 16
        : (perRow > 64 ? 64 : static_cast<std::size_t>(perRow));
    const std::size_t total = std::min<std::size_t>(bytes.size(), maxBytes);

    // The hex block of a full row, so a short last row can be padded to it and
    // the decode column still lines up with the rows above.
    std::size_t hexWidth = row * 3;
    for (std::size_t k = 8; k < row; k += 8)
        ++hexWidth; // the gap the eye uses to count

    std::string out;
    out.reserve((total / row + 1) * (hexWidth + row + 8));
    char buffer[8];
    for (std::size_t i = 0; i < total; i += row) {
        const std::size_t end = std::min<std::size_t>(i + row, total);
        std::string hex;
        std::string decoded;
        for (std::size_t j = i; j < end; ++j) {
            const unsigned char byte = static_cast<unsigned char>(bytes[j]);
            std::snprintf(buffer, sizeof buffer, "%02x ", byte);
            hex += buffer;
            // A gap every eight bytes: four columns of digits are hard to count.
            if ((j - i + 1) % 8 == 0 && j - i + 1 < row)
                hex += ' ';
            decoded += (byte >= 0x20 && byte < 0x7f) ? static_cast<char>(byte) : '.';
        }
        out += hex;
        if (ascii) {
            if (hex.size() < hexWidth)
                out.append(hexWidth - hex.size(), ' ');
            out += " |";
            out += decoded;
            out += '|';
        }
        out += '\n';
    }
    return out;
}

} // namespace tioplug
