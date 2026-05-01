#include "source_map.h"
#include <algorithm>
#include <cstdio>
#include <ostream>

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

}  // namespace

void SourceMap::render(int file_id, int tok, const std::string& msg, std::ostream& os) const {
    if (file_id < 0 || file_id >= (int)files_.size()) {
        os << "slidsc: error: " << msg << "\n";
        return;
    }
    // walk the imported_by chain root -> leaf
    std::vector<int> chain;
    for (int id = file_id; id != -1; id = files_[id].imported_by)
        chain.push_back(id);
    for (int i = (int)chain.size() - 1; i >= 1; i--)
        os << files_[chain[i]].path << ": imported\n";
    os << files_[chain[0]].path << ":\n";

    const SourceFile& f = files_[file_id];
    if (tok < 0 || tok >= (int)f.tokens.size()) {
        os << msg << "\n";
        return;
    }
    const TokenLoc& loc = f.tokens[tok];

    int first = std::max(1, loc.line - 3);
    int last = loc.line + 2;
    int max_line = (int)f.line_starts.size();
    if (last > max_line) last = max_line;
    // gutter width: enough digits for the largest line shown, minimum 2.
    int digit_w = std::max(2, (int)std::to_string(last).size());
    for (int ln = first; ln <= loc.line; ln++)
        printLine(os, ln, digit_w, getLine(f, ln));
    // caret line: gutter padding (digit_w digits + ':'), then loc.col-1 spaces, then markers
    for (int i = 0; i < digit_w + 1; i++) os << ' ';
    for (int i = 1; i < loc.col; i++) os << ' ';
    if (loc.length <= 1) {
        os << '^';
    } else if (loc.length == 2) {
        os << "^^";
    } else {
        os << '^';
        for (int i = 0; i < loc.length - 2; i++) os << '-';
        os << '^';
    }
    os << "\n";
    for (int ln = loc.line + 1; ln <= last; ln++)
        printLine(os, ln, digit_w, getLine(f, ln));
    os << msg << "\n";
}
