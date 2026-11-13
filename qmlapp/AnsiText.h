// AnsiText.h - turn a raw serial byte stream into displayable terminal text.
//
// A serial console is usually a VT100/ANSI terminal: the device (a Linux login
// prompt, a coloured shell, a TUI) sends escape sequences, not plain lines.
// Rendering those bytes verbatim is what produced "[7a[999;999H…" in the
// window. This reduces the stream to text plus a small set of style runs:
//
//   * CSI SGR (colours, bold, underline, inverse) sets the style of what follows;
//   * cursor movement, erase, mode set/reset and OSC titles are dropped;
//   * \r overwrites the current line from the start, \b steps back, \n starts a
//     new line, and \t expands;
//   * a trailing partial escape sequence is kept so the next read can finish it.
//
// This is deliberately not a full terminal emulator: it keeps the last screen
// lines a person wants to read, with the colours that carry meaning. Pure rules,
// no Windows and no Qt, so it is unit-tested directly.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tioplug {

// One style run inside a line.
//
// `color` is a 0xRRGGBB value, or -1 for the terminal's default foreground.
// `colorIndex` is set when the colour came from the 16-colour palette (SGR
// 30..37 / 90..97): the view resolves that index against its own theme, so a
// light-on-dark window can brighten colour 0 instead of drawing near-black on
// black. Colours 16..255 and truecolour are fixed RGB and carry no index.
struct AnsiSpan {
    std::string text;
    int color = -1;
    int colorIndex = -1;
    int background = -1;
    int backgroundIndex = -1;
    bool bold = false;
    bool inverse = false;
};

struct AnsiLine {
    std::vector<AnsiSpan> spans;
    bool hasText() const;
    std::string text() const;
};

class AnsiText {
public:
    // Appends newly received bytes and returns true when the visible screen
    // changed (so a view can repaint only then).
    bool feed(const char* data, std::size_t size);

    void clear();

    // The screen as lines of styled spans, oldest first.
    const std::vector<AnsiLine>& lines() const { return m_lines; }

    // Rows kept before the oldest is dropped.
    static constexpr std::size_t kMaxLines = 400;
    // Columns kept per line before text is dropped.
    static constexpr std::size_t kMaxColumns = 4096;

private:
    bool feedText(const char* data, std::size_t size);
    void put(char32_t ch);
    void carriageReturn();
    void backspace();
    void newline();
    void sgr(const std::vector<int>& params);
    void eraseLine(int mode);

    std::vector<AnsiLine> m_lines{{}};
    std::size_t m_row = 0;
    std::size_t m_col = 0;
    int m_color = -1;
    int m_colorIndex = -1;
    int m_background = -1;
    int m_backgroundIndex = -1;
    bool m_bold = false;
    bool m_inverse = false;
    // Bytes of an escape sequence that has not finished arriving.
    std::string m_pending;
    bool m_dirty = false;
};

// Formats raw bytes for the plain-text and hex views.
//
// `perRow` is the bytes on one line (8, 16 or 32 from the window; clamped to
// 1..64 here). `ascii` adds a decode column on the right: printable bytes as
// themselves, everything else as '.', which is what makes a binary frame
// readable. Short rows are padded so that column stays aligned.
std::string asHexDump(const std::string& bytes, std::size_t maxBytes = 64 * 1024,
                      int perRow = 16, bool ascii = false);

// The 16 ANSI palette colours every terminal starts from, as 0xRRGGBB.
// kAnsiPalette[i] is what SGR 30+i (or 90+i-8) names. Exposed so the view can
// pick its own variant on a dark background.
extern const int kAnsiPalette[16];

// The standard xterm 256-colour table: 0..15 are the palette above, 16..231 are
// a 6x6x6 cube, 232..255 are a grey ramp. Returns 0xRRGGBB.
int ansi256(int index);

} // namespace tioplug
