// TextReader — C# StreamReader semantics over an in-memory buffer, so the
// ported parsing code (CsvDefinition::ParseNextLine etc.) can stay line-for-
// line faithful to the original. Matches: Read() consumes one char, Peek()
// looks ahead (-1 at end), ReadLine() strips \r\n | \n | \r, EndOfStream.
#pragma once
#include <string>

class TextReader {
public:
    explicit TextReader(const std::string &text) : _s(text) {}

    bool EndOfStream() const { return _pos >= _s.size(); }

    int Peek() const { return EndOfStream() ? -1 : (unsigned char)_s[_pos]; }

    int Read() { return EndOfStream() ? -1 : (unsigned char)_s[_pos++]; }

    /// C# ReadLine: returns text up to the next line break, consuming the
    /// break (\r\n as one). Returns false at end-of-stream (C# returns null).
    bool ReadLine(std::string &line) {
        if (EndOfStream()) return false;
        line.clear();
        while (!EndOfStream()) {
            char c = _s[_pos++];
            if (c == '\r') {
                if (!EndOfStream() && _s[_pos] == '\n') _pos++;
                return true;
            }
            if (c == '\n') return true;
            line.push_back(c);
        }
        return true;
    }

private:
    const std::string &_s;
    size_t _pos = 0;
};
