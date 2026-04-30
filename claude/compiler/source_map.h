#pragma once
#include <exception>
#include <iosfwd>
#include <string>
#include <vector>

struct TokenLoc {
    int line = 0;
    int col = 0;
    int length = 0;
};

struct SourceFile {
    std::string path;
    std::string source;
    std::vector<int> line_starts;     // byte offset of each line's first char (line 1 → [0])
    std::vector<TokenLoc> tokens;     // index-aligned with the parser's token vector
    int imported_by = -1;             // parent file_id; -1 = root
};

class SourceMap {
    std::vector<SourceFile> files_;
public:
    int openFile(std::string path, std::string source, int imported_by = -1);
    SourceFile& at(int file_id);
    const SourceFile& at(int file_id) const;
    void render(int file_id, int tok, const std::string& msg, std::ostream& os) const;
};

struct CompileError : std::exception {
    int file_id;
    int tok;
    std::string msg;
    CompileError(int f, int t, std::string m)
        : file_id(f), tok(t), msg(std::move(m)) {}
    const char* what() const noexcept override { return msg.c_str(); }
};
