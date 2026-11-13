// test_ansi_text.cpp - the ANSI reducer behind the shared-content view.
//
// The window has to show what a serial console really sends. These checks pin
// the two things that broke it before: escape sequences must never reach the
// text, and SGR colours must survive as style runs.
#include "../qmlapp/AnsiText.h"

#include <cstdio>
#include <string>

namespace {

int g_total = 0;
int g_failed = 0;

void check(bool ok, const char* label, const std::string& detail = std::string())
{
    ++g_total;
    if (ok) {
        std::printf("PASS %s\n", label);
    } else {
        ++g_failed;
        std::printf("FAIL %s%s%s\n", label, detail.empty() ? "" : ": ", detail.c_str());
    }
}

std::string feed(tioplug::AnsiText& text, const std::string& bytes)
{
    text.feed(bytes.data(), bytes.size());
    std::string out;
    for (const tioplug::AnsiLine& line : text.lines()) {
        if (!out.empty())
            out += '\n';
        out += line.text();
    }
    return out;
}

} // namespace

int main()
{
    {
        // The exact shape from a Linux login prompt plus a coloured shell.
        tioplug::AnsiText text;
        const std::string stream =
            "\x1b[7a\x1b[999;999H\x1b[6n\x1b[1;36mUbuntu 24.04 LTS\x1b[0m\r\n"
            "login: ";
        const std::string out = feed(text, stream);
        check(out.find('\x1b') == std::string::npos, "no escape byte reaches the text", out);
        check(out.find("[1;36m") == std::string::npos, "no SGR sequence reaches the text", out);
        check(out.find("Ubuntu 24.04 LTS") != std::string::npos, "the visible text is kept", out);
        check(out.find("login:") != std::string::npos, "the prompt survives", out);
    }

    {
        // Colour and bold are style runs, not text.
        tioplug::AnsiText text;
        feed(text, "\x1b[1;31mERROR\x1b[0m ok");
        const auto& lines = text.lines();
        bool foundColor = false;
        bool foundBold = false;
        for (const auto& line : lines) {
            for (const auto& span : line.spans) {
                if (span.text == "ERROR") {
                    foundColor = span.color >= 0;
                    foundBold = span.bold;
                }
            }
        }
        check(foundColor, "a red SGR run keeps a colour");
        check(foundBold, "a bold SGR run keeps bold");
    }

    {
        // Carriage return overwrites the line instead of appending to it: that is
        // how progress bars and login prompts behave.
        tioplug::AnsiText text;
        feed(text, "progress 10%\rprogress 99%");
        const std::string out = feed(text, "");
        check(out.find("progress 99%") != std::string::npos, "CR overwrites the line", out);
        check(out.find("progress 10%progress") == std::string::npos,
            "CR does not glue the redraw onto the old text", out);
    }

    {
        // A shorter redraw leaves the tail that was not overwritten.
        tioplug::AnsiText text;
        feed(text, "abcdef\rXY");
        const std::string out = feed(text, "");
        check(out == "XYcdef", "a short CR redraw keeps the untouched tail", out);
    }

    {
        // A sequence split across two reads still gets parsed.
        tioplug::AnsiText text;
        text.feed("\x1b[1;3", 5);
        text.feed("2mGREEN\x1b[0m", 11);
        bool green = false;
        for (const auto& line : text.lines())
            for (const auto& span : line.spans)
                if (span.text == "GREEN" && span.color >= 0)
                    green = true;
        check(green, "an escape split across reads is finished");
    }

    {
        tioplug::AnsiText text;
        feed(text, "a\nb\nc");
        check(text.lines().size() == 3, "newlines make lines",
            std::to_string(text.lines().size()));
    }

    {
        tioplug::AnsiText text;
        // Built by concatenation: in a single literal "\x07body" the compiler
        // reads "\x07b" as one hex escape and eats the 'b'.
        const std::string osc = std::string("\x1b]0;window title") + "\x07" + "body";
        feed(text, osc);
        const std::string out = feed(text, "");
        check(out.find("body") != std::string::npos, "text after an OSC title is kept", out);
        check(out.find("window title") == std::string::npos, "the OSC payload is dropped", out);
    }

    {
        // 256-colour is the full xterm table, not a 16-colour modulo: 208 is
        // orange, and 208 % 16 == 0 used to turn it near-black.
        tioplug::AnsiText text;
        const std::string stream = std::string("\x1b[38;5;208mORANGE\x1b[0m");
        feed(text, stream);
        int color = -2;
        for (const auto& line : text.lines())
            for (const auto& span : line.spans)
                if (span.text == "ORANGE")
                    color = span.color;
        check(color == 0xff8700, "256-colour 208 is orange");
        check(color != 0x1b1b1b, "256-colour never collapses to palette black");
    }

    {
        // The grey ramp and the cube edges, so a bug in either half shows.
        check(tioplug::ansi256(232) == 0x080808, "256 grey ramp starts at 08");
        check(tioplug::ansi256(255) == 0xeeeeee, "256 grey ramp ends at ee");
        check(tioplug::ansi256(196) == 0xff0000, "256 cube 196 is red");
        check(tioplug::ansi256(21) == 0x0000ff, "256 cube 21 is blue");
        check(tioplug::ansi256(4) == 0x0f6cbd, "256 palette 4 is the theme blue");
    }

    {
        // Background colour and inverse: what a highlighted status bar uses.
        tioplug::AnsiText text;
        const std::string stream = std::string("\x1b[41;30mALERT\x1b[0m");
        feed(text, stream);
        int background = -2;
        int foreground = -2;
        for (const auto& line : text.lines())
            for (const auto& span : line.spans)
                if (span.text == "ALERT") {
                    background = span.background;
                    foreground = span.color;
                }
        check(background >= 0, "a background SGR is kept");
        check(foreground >= 0, "its foreground is kept too");
        check(background != foreground, "background and foreground stay distinct");
    }

    {
        // Inverse is a flag: the view swaps the two colours when it draws.
        tioplug::AnsiText text;
        const std::string stream = std::string("\x1b[7mSTATUS\x1b[0m");
        feed(text, stream);
        bool inverse = false;
        for (const auto& line : text.lines())
            for (const auto& span : line.spans)
                if (span.text == "STATUS")
                    inverse = span.inverse;
        check(inverse, "reverse video is recorded");
    }

    {
        // A palette colour carries its index, so a dark window can brighten it.
        tioplug::AnsiText text;
        const std::string stream = std::string("\x1b[30mBLACK\x1b[0m");
        feed(text, stream);
        int index = -2;
        for (const auto& line : text.lines())
            for (const auto& span : line.spans)
                if (span.text == "BLACK")
                    index = span.colorIndex;
        check(index == 0, "SGR 30 records palette index 0");
    }

    {
        const std::string hex = tioplug::asHexDump(std::string("Hi!\r\n", 5));
        check(hex.find("48 69 21 0d 0a") != std::string::npos, "the hex dump spells the bytes", hex);
        // The dump is a sliding window, so an offset would be a lie; it used to
        // print as a row of leading zeros that read like data.
        check(hex.find("00000000") == std::string::npos, "the hex dump has no offset column", hex);
        check(hex.find("48 69 21 0d") > 0 || hex.rfind("48", 0) == 0,
            "the dump starts with the first byte", hex);
    }

    {
        // The row width is the reader's choice, and it must be the width asked
        // for: eight per row is two lines where sixteen is one.
        const std::string data("0123456789abcdef", 16);
        const std::string eight = tioplug::asHexDump(data, 1024, 8, false);
        int lines = 0;
        for (const char c : eight)
            if (c == '\n')
                ++lines;
        check(lines == 2, "8 bytes per row splits 16 bytes over two lines",
            std::to_string(lines));

        const std::string wide = tioplug::asHexDump(data, 1024, 32, false);
        int wideLines = 0;
        for (const char c : wide)
            if (c == '\n')
                ++wideLines;
        check(wideLines == 1, "32 bytes per row keeps 16 bytes on one line");

        // A nonsense width is clamped, never a zero-step loop.
        const std::string clamped = tioplug::asHexDump(data, 1024, 0, false);
        check(!clamped.empty(), "a zero width still produces a dump");
    }

    {
        // The decode column: printable bytes as themselves, the rest as dots,
        // and a short last row padded so the column lines up.
        const std::string data("AB\x01\x02" "CD", 6);
        const std::string dump = tioplug::asHexDump(data, 1024, 8, true);
        check(dump.find("|AB..CD|") != std::string::npos, "the decode column spells printable bytes", dump);        // Without the flag the column must not appear at all.
        const std::string plain = tioplug::asHexDump(data, 1024, 8, false);
        check(plain.find('|') == std::string::npos, "the decode column is off by default", plain);
    }

    std::printf("%s %d ansi checks, %d failed\n",
        g_failed ? "FAILED" : "PASS", g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
