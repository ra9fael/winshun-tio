// probe_ansi.cpp - renders the exact bytes from a real Linux serial console.
//
// The stream is copied from the conversation: an ANSI login prompt with cursor
// reports, a coloured shell prompt, and a command. It prints the reduced lines
// and the colour runs, which is what the window's terminal view draws.
#include "../qmlapp/AnsiText.h"

#include <cstdio>
#include <string>

namespace {

std::string visible(const tioplug::AnsiText& text)
{
    std::string out;
    for (const auto& line : text.lines()) {
        if (!out.empty())
            out += '\n';
        out += line.text();
    }
    return out;
}

std::size_t colorRuns(const tioplug::AnsiText& text)
{
    std::size_t runs = 0;
    for (const auto& line : text.lines())
        for (const auto& span : line.spans)
            if (span.color >= 0 || span.bold)
                ++runs;
    return runs;
}

} // namespace

int main()
{
    // Verbatim from the console, with ESC spelled \x1b.
    const std::string stream =
        "XRAY-BLMee login: plnx\r\n"
        "Password:\r\n"
        "\x1b[7a\x1b[999;999H\x1b[6n"
        "38\r\n"
        "a\x1b[1;36m\x1b[2Jubuntu@xray\x1b[0m:\x1b[1;34m~\x1b[0m$ ls\r\n"
        "\x1b[1;31m>\x1b[0m file1  file2\r\n";

    tioplug::AnsiText text;
    text.feed(stream.data(), stream.size());

    std::printf("lines=%zu colorRuns=%zu\n", text.lines().size(), colorRuns(text));
    std::printf("--- rendered ---\n%s\n--- end ---\n", visible(text).c_str());

    const std::string out = visible(text);
    const bool noEscapes = out.find('\x1b') == std::string::npos
        && out.find("[1;36m") == std::string::npos && out.find("[0m") == std::string::npos;
    const bool keptText = out.find("login: plnx") != std::string::npos
        && out.find("ubuntu@xray") != std::string::npos
        && out.find("$ ls") != std::string::npos
        && out.find("file1  file2") != std::string::npos;

    std::printf("no escape text : %s\n", noEscapes ? "PASS" : "FAIL");
    std::printf("kept the text  : %s\n", keptText ? "PASS" : "FAIL");
    std::printf("kept colours   : %s\n", colorRuns(text) > 0 ? "PASS" : "FAIL");
    return noEscapes && keptText ? 0 : 1;
}
