#include "source_map.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ostream>
#include <unistd.h>

int SourceMap::openFile(std::string path, std::string source, int imported_by) {
    SourceFile sf;
    sf.path = std::move(path);
    sf.source = std::move(source);
    sf.imported_by = imported_by;
    sf.line_starts.push_back(0);
    for (int i = 0; i < (int)sf.source.size(); i++) {
        if (sf.source[i] == '\n') sf.line_starts.push_back(i + 1);
    }
    int id = (int)files_.size();
    files_.push_back(std::move(sf));
    return id;
}

SourceFile& SourceMap::at(int file_id) {
    return files_.at(file_id);
}

const SourceFile& SourceMap::at(int file_id) const {
    return files_.at(file_id);
}

namespace {

bool colorOn() {
    static const bool on = []{
        if (const char* nc = std::getenv("NO_COLOR"); nc && *nc) return false;
        return isatty(STDERR_FILENO) != 0;
    }();
    return on;
}
const char* cBold()    { return colorOn() ? "\033[1m"  : ""; }
const char* cRed()     { return colorOn() ? "\033[38;5;160m" : ""; }
const char* cYellow()  { return colorOn() ? "\033[38;5;226m" : ""; }
const char* cBlueish() { return colorOn() ? "\033[38;5;73m"  : ""; }
const char* cReset()   { return colorOn() ? "\033[0m"  : ""; }

std::string getLine(const SourceFile& f, int line) {
    if (line < 1 || line > (int)f.line_starts.size()) return "";
    int start = f.line_starts[line - 1];
    int end = (line < (int)f.line_starts.size())
        ? f.line_starts[line] - 1   // exclude '\n'
        : (int)f.source.size();
    if (end > 0 && end <= (int)f.source.size()
        && f.source[end - 1] == '\r') end--;   // strip CR for CRLF files
    return f.source.substr(start, end - start);
}

void printLine(std::ostream& os, int line_num, int digit_w, const std::string& text) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%*d:", digit_w, line_num);
    os << buf << text << "\n";
}

void renderBlock(const std::vector<SourceFile>& files,
                 int file_id, int tok, const std::string& msg,
                 std::ostream& os) {
    if (file_id < 0 || file_id >= (int)files.size()) {
        os << "slidsc: error: " << cYellow() << msg << cReset() << "\n";
        return;
    }
    // walk the imported_by chain root -> leaf
    std::vector<int> chain;
    for (int id = file_id; id != -1; id = files[id].imported_by)
        chain.push_back(id);
    for (int i = (int)chain.size() - 1; i >= 1; i--)
        os << files[chain[i]].path << ": imported\n";
    os << cBold() << files[chain[0]].path << ":" << cReset() << "\n";

    const SourceFile& f = files[file_id];
    if (tok < 0 || tok >= (int)f.tokens.size()) {
        os << cYellow() << msg << cReset() << "\n";
        return;
    }
    const TokenLoc& loc = f.tokens[tok];

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

void SourceMap::render(const CompileError& e, std::ostream& os) const {
    renderBlock(files_, e.file_id, e.tok, e.msg, os);
    for (auto& n : e.notes) {
        os << "\n";
        renderBlock(files_, n.file_id, n.tok, n.msg, os);
    }
}
