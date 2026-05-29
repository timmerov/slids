#include "diagnostic.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ostream>
#include <string>
#include <unistd.h>
#include <vector>

#include "token.h"

namespace diagnostic {

void report(Sink& sink, Record const& record) {
    sink.records.push_back(record);
}

bool hasErrors(Sink const& sink) {
    return !sink.records.empty();
}

namespace {

bool colorOn() {
    static const bool on = []{
        if (const char* nc = std::getenv("NO_COLOR"); nc && *nc) return false;
        return isatty(STDERR_FILENO) != 0;
    }();
    return on;
}
char const* cBold()    { return colorOn() ? "\033[1m"        : ""; }
char const* cRed()     { return colorOn() ? "\033[38;5;160m" : ""; }
char const* cYellow()  { return colorOn() ? "\033[38;5;226m" : ""; }
char const* cBlueish() { return colorOn() ? "\033[38;5;73m"  : ""; }
char const* cReset()   { return colorOn() ? "\033[0m"        : ""; }

std::string getLine(token::File const& f, int line) {
    if (line < 1 || line > (int)f.line_starts.size()) return "";
    int start = f.line_starts[line - 1];
    int end = (line < (int)f.line_starts.size())
        ? f.line_starts[line] - 1   // exclude '\n'
        : (int)f.source.size();
    if (end > 0 && end <= (int)f.source.size()
        && f.source[end - 1] == '\r') end--;   // strip CR for CRLF files
    return f.source.substr(start, end - start);
}

void printLine(std::ostream& os, int line_num, int digit_w, std::string const& text) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%*d:", digit_w, line_num);
    os << buf << text << "\n";
}

void renderBlock(token::List const& tokens, int file_id, int tok,
                 std::string const& msg, std::ostream& os) {
    if (file_id < 0 || file_id >= (int)tokens.files.size()) {
        os << "slidsc: error: " << cYellow() << msg << cReset() << "\n";
        return;
    }
    // walk the imported_by chain root -> leaf
    std::vector<int> chain;
    for (int id = file_id; id != -1; id = tokens.files[id].imported_by)
        chain.push_back(id);
    for (int i = (int)chain.size() - 1; i >= 1; i--)
        os << tokens.files[chain[i]].path << ": imported\n";
    os << cBold() << tokens.files[chain[0]].path << ":" << cReset() << "\n";

    if (tok < 0 || tok >= (int)tokens.tokens.size()) {
        os << cYellow() << msg << cReset() << "\n";
        return;
    }
    token::Token const& loc = tokens.tokens[tok];
    token::File const& f = tokens.files[file_id];

    int first = std::max(1, loc.line - 3);
    int last = loc.line + 2;
    int max_line = (int)f.line_starts.size();
    if (last > max_line) last = max_line;
    int digit_w = std::max(2, (int)std::to_string(last).size());
    for (int ln = first; ln < loc.line; ln++)
        printLine(os, ln, digit_w, getLine(f, ln));
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%*d:", digit_w, loc.line);
        std::string text = getLine(f, loc.line);
        int cstart = std::max(0, loc.col - 1);
        if (cstart > (int)text.size()) cstart = (int)text.size();
        int avail = (int)text.size() - cstart;
        int clen = std::max(0, std::min(loc.length, avail));
        os << cBlueish() << buf
           << text.substr(0, cstart)
           << cRed() << text.substr(cstart, clen)
           << cBlueish() << text.substr(cstart + clen)
           << cReset() << "\n";
    }
    for (int i = 0; i < digit_w + 1; i++) os << ' ';
    for (int i = 1; i < loc.col; i++) os << ' ';
    os << cRed();
    if (loc.length <= 1) {
        os << '^';
    } else if (loc.length == 2) {
        os << "^^";
    } else {
        os << '^';
        for (int i = 0; i < loc.length - 2; i++) os << '-';
        os << '^';
    }
    os << cReset() << "\n";
    for (int ln = loc.line + 1; ln <= last; ln++)
        printLine(os, ln, digit_w, getLine(f, ln));
    os << cYellow() << msg << cReset() << "\n";
}

}  // namespace

void render(token::List const& tokens, Sink const& sink, std::ostream& os) {
    bool first = true;
    for (auto const& r : sink.records) {
        if (!first) os << "\n";
        renderBlock(tokens, r.file_id, r.tok, r.message, os);
        for (auto const& n : r.notes) {
            os << "\n";
            renderBlock(tokens, n.file_id, n.tok, n.message, os);
        }
        first = false;
    }
}

}  // namespace diagnostic
