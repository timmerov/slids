#include "token.h"

#include <utility>

namespace token {

void add(List& list, Token const& tok) {
    list.tokens.push_back(tok);
}

int openFile(List& list, std::string path, std::string source, int imported_by) {
    File f;
    f.path = std::move(path);
    f.source = std::move(source);
    f.imported_by = imported_by;
    f.line_starts.push_back(0);
    for (int i = 0; i < (int)f.source.size(); i++) {
        if (f.source[i] == '\n') f.line_starts.push_back(i + 1);
    }
    int id = (int)list.files.size();
    list.files.push_back(std::move(f));
    return id;
}

}  // namespace token
